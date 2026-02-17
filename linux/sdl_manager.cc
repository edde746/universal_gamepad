#include "sdl_manager.h"

#include "button_mapping.h"

#include <chrono>
#include <cstring>

// Polling interval: ~60 Hz => ~16 ms.
static constexpr auto kPollInterval = std::chrono::milliseconds(16);

SdlManager::SdlManager() = default;

SdlManager::~SdlManager() { Stop(); }

void SdlManager::Start(EventCallback callback) {
  callback_ = std::make_shared<EventCallback>(std::move(callback));

#if HAVE_SDL3
  if (running_.load()) return;
  running_.store(true);
  poll_thread_ = std::thread(&SdlManager::PollLoop, this);
#else
  g_warning(
      "gamepad: SDL3 not available. Gamepad support is disabled.");
#endif
}

void SdlManager::Stop() {
#if HAVE_SDL3
  running_.store(false);
  if (poll_thread_.joinable()) {
    poll_thread_.join();
  }
#endif

  // Nullify the std::function inside the shared_ptr before releasing our
  // reference.  In-flight idle callbacks hold their own shared_ptr copies
  // pointing to the same object; they will see an empty function and no-op.
  if (callback_) {
    *callback_ = nullptr;
  }
  callback_.reset();
}

FlValue* SdlManager::ListGamepads() {
  FlValue* list = fl_value_new_list();

#if HAVE_SDL3
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& [id, info] : gamepads_) {
    FlValue* map = fl_value_new_map();
    std::string gamepad_id = MakeGamepadId(id);

    fl_value_set_string_take(map, "id",
                             fl_value_new_string(gamepad_id.c_str()));
    fl_value_set_string_take(map, "name",
                             fl_value_new_string(info.name.c_str()));
    fl_value_set_string_take(map, "vendorId",
                             fl_value_new_int(info.vendor_id));
    fl_value_set_string_take(map, "productId",
                             fl_value_new_int(info.product_id));

    fl_value_append_take(list, map);
  }
#endif

  return list;
}

#if HAVE_SDL3

std::string SdlManager::MakeGamepadId(SDL_JoystickID id) {
  return "linux_" + std::to_string(id);
}

int64_t SdlManager::NowMillis() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();
  return static_cast<int64_t>(ms);
}

void SdlManager::PollLoop() {
  if (!SDL_Init(SDL_INIT_GAMEPAD)) {
    g_warning("gamepad: SDL_Init(SDL_INIT_GAMEPAD) failed: %s",
              SDL_GetError());
    running_.store(false);
    return;
  }

  while (running_.load()) {
    PollEvents();
    std::this_thread::sleep_for(kPollInterval);
  }

  // Cleanup: close all gamepads and quit the subsystem.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, info] : gamepads_) {
      if (info.gamepad) {
        SDL_CloseGamepad(info.gamepad);
      }
    }
    gamepads_.clear();
  }

  SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void SdlManager::PollEvents() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_EVENT_GAMEPAD_ADDED:
        HandleGamepadAdded(event.gdevice.which);
        break;
      case SDL_EVENT_GAMEPAD_REMOVED:
        HandleGamepadRemoved(event.gdevice.which);
        break;
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        HandleButtonEvent(event.gbutton.which, event.gbutton.button, true);
        break;
      case SDL_EVENT_GAMEPAD_BUTTON_UP:
        HandleButtonEvent(event.gbutton.which, event.gbutton.button, false);
        break;
      case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        HandleAxisEvent(event.gaxis.which, event.gaxis.axis, event.gaxis.value);
        break;
      default:
        break;
    }
  }
}

void SdlManager::DispatchEvent(FlValue* event) {
  fl_value_ref(event);
  auto* data = new IdleEventData{event, callback_};
  g_idle_add(IdleDispatchCallback, data);
}

gboolean SdlManager::IdleDispatchCallback(gpointer user_data) {
  auto* data = static_cast<IdleEventData*>(user_data);
  if (data->callback && *data->callback) {
    (*data->callback)(data->event);
  }
  fl_value_unref(data->event);
  delete data;
  return G_SOURCE_REMOVE;
}

void SdlManager::HandleGamepadAdded(SDL_JoystickID joystick_id) {
  SDL_Gamepad* gamepad = SDL_OpenGamepad(joystick_id);
  if (!gamepad) {
    g_warning("gamepad: failed to open gamepad %d: %s",
              joystick_id, SDL_GetError());
    return;
  }

  const char* name = SDL_GetGamepadName(gamepad);
  uint16_t vendor = SDL_GetGamepadVendor(gamepad);
  uint16_t product = SDL_GetGamepadProduct(gamepad);

  GamepadInfo info;
  info.joystick_id = joystick_id;
  info.gamepad = gamepad;
  info.name = name ? name : "Unknown Gamepad";
  info.vendor_id = vendor;
  info.product_id = product;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    gamepads_[joystick_id] = info;
  }

  std::string gid = MakeGamepadId(joystick_id);
  int64_t ts = NowMillis();

  FlValue* event = fl_value_new_map();
  fl_value_set_string_take(event, "type",
                           fl_value_new_string("connection"));
  fl_value_set_string_take(event, "gamepadId",
                           fl_value_new_string(gid.c_str()));
  fl_value_set_string_take(event, "timestamp", fl_value_new_int(ts));
  fl_value_set_string_take(event, "connected", fl_value_new_bool(TRUE));
  fl_value_set_string_take(event, "name",
                           fl_value_new_string(info.name.c_str()));
  fl_value_set_string_take(event, "vendorId",
                           fl_value_new_int(info.vendor_id));
  fl_value_set_string_take(event, "productId",
                           fl_value_new_int(info.product_id));
  DispatchEvent(event);
  fl_value_unref(event);
}

void SdlManager::HandleGamepadRemoved(SDL_JoystickID joystick_id) {
  GamepadInfo info;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = gamepads_.find(joystick_id);
    if (it == gamepads_.end()) {
      return;
    }
    info = it->second;
    if (info.gamepad) {
      SDL_CloseGamepad(info.gamepad);
    }
    gamepads_.erase(it);
  }

  std::string gid = MakeGamepadId(joystick_id);
  int64_t ts = NowMillis();

  FlValue* event = fl_value_new_map();
  fl_value_set_string_take(event, "type",
                           fl_value_new_string("connection"));
  fl_value_set_string_take(event, "gamepadId",
                           fl_value_new_string(gid.c_str()));
  fl_value_set_string_take(event, "timestamp", fl_value_new_int(ts));
  fl_value_set_string_take(event, "connected", fl_value_new_bool(FALSE));
  fl_value_set_string_take(event, "name",
                           fl_value_new_string(info.name.c_str()));
  fl_value_set_string_take(event, "vendorId",
                           fl_value_new_int(info.vendor_id));
  fl_value_set_string_take(event, "productId",
                           fl_value_new_int(info.product_id));
  DispatchEvent(event);
  fl_value_unref(event);
}

void SdlManager::HandleButtonEvent(SDL_JoystickID joystick_id, uint8_t button,
                                   bool pressed) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (gamepads_.find(joystick_id) == gamepads_.end()) {
      return;
    }
  }

  int w3c_index =
      ButtonMapping::SdlButtonToW3C(static_cast<SDL_GamepadButton>(button));
  if (w3c_index < 0) {
    return;
  }

  std::string gid = MakeGamepadId(joystick_id);
  int64_t ts = NowMillis();

  FlValue* event = fl_value_new_map();
  fl_value_set_string_take(event, "type", fl_value_new_string("button"));
  fl_value_set_string_take(event, "gamepadId",
                           fl_value_new_string(gid.c_str()));
  fl_value_set_string_take(event, "timestamp", fl_value_new_int(ts));
  fl_value_set_string_take(event, "button", fl_value_new_int(w3c_index));
  fl_value_set_string_take(event, "pressed",
                           fl_value_new_bool(pressed ? TRUE : FALSE));
  fl_value_set_string_take(event, "value",
                           fl_value_new_float(pressed ? 1.0 : 0.0));
  DispatchEvent(event);
  fl_value_unref(event);
}

void SdlManager::HandleAxisEvent(SDL_JoystickID joystick_id, uint8_t axis,
                                 int16_t value) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (gamepads_.find(joystick_id) == gamepads_.end()) {
      return;
    }
  }

  auto sdl_axis = static_cast<SDL_GamepadAxis>(axis);

  // Check if this axis is a trigger (maps to analog button, not a stick axis).
  if (ButtonMapping::IsTriggerAxis(sdl_axis)) {
    int button_index = ButtonMapping::TriggerAxisToButtonIndex(sdl_axis);
    if (button_index < 0) {
      return;
    }

    double normalized = ButtonMapping::NormalizeTriggerAxis(value);
    bool pressed = normalized > 0.5;

    std::string gid = MakeGamepadId(joystick_id);
    int64_t ts = NowMillis();

    FlValue* event = fl_value_new_map();
    fl_value_set_string_take(event, "type", fl_value_new_string("button"));
    fl_value_set_string_take(event, "gamepadId",
                             fl_value_new_string(gid.c_str()));
    fl_value_set_string_take(event, "timestamp", fl_value_new_int(ts));
    fl_value_set_string_take(event, "button",
                             fl_value_new_int(button_index));
    fl_value_set_string_take(event, "pressed",
                             fl_value_new_bool(pressed ? TRUE : FALSE));
    fl_value_set_string_take(event, "value",
                             fl_value_new_float(normalized));
    DispatchEvent(event);
    fl_value_unref(event);
    return;
  }

  // Regular stick axis.
  int w3c_index = ButtonMapping::SdlAxisToW3C(sdl_axis);
  if (w3c_index < 0) {
    return;
  }

  double normalized = ButtonMapping::NormalizeStickAxis(value);

  std::string gid = MakeGamepadId(joystick_id);
  int64_t ts = NowMillis();

  FlValue* event = fl_value_new_map();
  fl_value_set_string_take(event, "type", fl_value_new_string("axis"));
  fl_value_set_string_take(event, "gamepadId",
                           fl_value_new_string(gid.c_str()));
  fl_value_set_string_take(event, "timestamp", fl_value_new_int(ts));
  fl_value_set_string_take(event, "axis", fl_value_new_int(w3c_index));
  fl_value_set_string_take(event, "value", fl_value_new_float(normalized));
  DispatchEvent(event);
  fl_value_unref(event);
}

#endif  // HAVE_SDL3
