#include "sdl_manager.h"

#include "gamepad_stream_handler.h"

#include <chrono>

namespace gamepad {

// Polling interval: ~60 Hz => ~16 ms.
static constexpr auto kPollInterval = std::chrono::milliseconds(16);

SdlManager::SdlManager(
    std::shared_ptr<GamepadStreamHandler> stream_handler)
    : stream_handler_(std::move(stream_handler)) {}

SdlManager::~SdlManager() { StopPolling(); }

void SdlManager::StartPolling() {
  if (running_.load()) return;
  running_.store(true);
  poll_thread_ = std::thread(&SdlManager::PollLoop, this);
}

void SdlManager::StopPolling() {
  running_.store(false);
  if (poll_thread_.joinable()) {
    poll_thread_.join();
  }
}

flutter::EncodableList SdlManager::ListGamepads() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  flutter::EncodableList result;

  for (const auto& [id, info] : gamepads_) {
    flutter::EncodableMap map;
    map[flutter::EncodableValue("id")] =
        flutter::EncodableValue(MakeGamepadId(id));
    map[flutter::EncodableValue("name")] =
        flutter::EncodableValue(info.name);
    map[flutter::EncodableValue("vendorId")] =
        flutter::EncodableValue(static_cast<int32_t>(info.vendor_id));
    map[flutter::EncodableValue("productId")] =
        flutter::EncodableValue(static_cast<int32_t>(info.product_id));
    result.push_back(flutter::EncodableValue(map));
  }
  return result;
}

void SdlManager::PollLoop() {
  if (!SDL_Init(SDL_INIT_GAMEPAD)) {
    // SDL_Init failed; cannot poll gamepads.
    running_.store(false);
    return;
  }

  while (running_.load()) {
    PollEvents();
    std::this_thread::sleep_for(kPollInterval);
  }

  // Cleanup: close all gamepads and quit the subsystem.
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
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
        HandleAxisEvent(event.gaxis.which, event.gaxis.axis,
                        event.gaxis.value);
        break;
      default:
        break;
    }
  }
}

void SdlManager::HandleGamepadAdded(SDL_JoystickID joystick_id) {
  SDL_Gamepad* gamepad = SDL_OpenGamepad(joystick_id);
  if (!gamepad) {
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
    std::lock_guard<std::mutex> lock(state_mutex_);
    gamepads_[joystick_id] = info;
  }

  // Emit connection event.
  flutter::EncodableMap event;
  event[flutter::EncodableValue("type")] =
      flutter::EncodableValue(std::string("connection"));
  event[flutter::EncodableValue("gamepadId")] =
      flutter::EncodableValue(MakeGamepadId(joystick_id));
  event[flutter::EncodableValue("timestamp")] =
      flutter::EncodableValue(CurrentTimestamp());
  event[flutter::EncodableValue("connected")] =
      flutter::EncodableValue(true);
  event[flutter::EncodableValue("name")] =
      flutter::EncodableValue(info.name);
  event[flutter::EncodableValue("vendorId")] =
      flutter::EncodableValue(static_cast<int32_t>(info.vendor_id));
  event[flutter::EncodableValue("productId")] =
      flutter::EncodableValue(static_cast<int32_t>(info.product_id));

  stream_handler_->SendEvent(flutter::EncodableValue(event));
}

void SdlManager::HandleGamepadRemoved(SDL_JoystickID joystick_id) {
  GamepadInfo info;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
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

  // Emit disconnection event.
  flutter::EncodableMap event;
  event[flutter::EncodableValue("type")] =
      flutter::EncodableValue(std::string("connection"));
  event[flutter::EncodableValue("gamepadId")] =
      flutter::EncodableValue(MakeGamepadId(joystick_id));
  event[flutter::EncodableValue("timestamp")] =
      flutter::EncodableValue(CurrentTimestamp());
  event[flutter::EncodableValue("connected")] =
      flutter::EncodableValue(false);
  event[flutter::EncodableValue("name")] =
      flutter::EncodableValue(info.name);
  event[flutter::EncodableValue("vendorId")] =
      flutter::EncodableValue(static_cast<int32_t>(info.vendor_id));
  event[flutter::EncodableValue("productId")] =
      flutter::EncodableValue(static_cast<int32_t>(info.product_id));

  stream_handler_->SendEvent(flutter::EncodableValue(event));
}

void SdlManager::HandleButtonEvent(SDL_JoystickID joystick_id, uint8_t button,
                                   bool pressed) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (gamepads_.find(joystick_id) == gamepads_.end()) {
      return;
    }
  }

  int w3c_index = SdlButtonToW3C(static_cast<SDL_GamepadButton>(button));
  if (w3c_index < 0) {
    return;
  }

  flutter::EncodableMap event;
  event[flutter::EncodableValue("type")] =
      flutter::EncodableValue(std::string("button"));
  event[flutter::EncodableValue("gamepadId")] =
      flutter::EncodableValue(MakeGamepadId(joystick_id));
  event[flutter::EncodableValue("timestamp")] =
      flutter::EncodableValue(CurrentTimestamp());
  event[flutter::EncodableValue("button")] =
      flutter::EncodableValue(static_cast<int32_t>(w3c_index));
  event[flutter::EncodableValue("pressed")] =
      flutter::EncodableValue(pressed);
  event[flutter::EncodableValue("value")] =
      flutter::EncodableValue(pressed ? 1.0 : 0.0);

  stream_handler_->SendEvent(flutter::EncodableValue(event));
}

void SdlManager::HandleAxisEvent(SDL_JoystickID joystick_id, uint8_t axis,
                                 int16_t value) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (gamepads_.find(joystick_id) == gamepads_.end()) {
      return;
    }
  }

  auto sdl_axis = static_cast<SDL_GamepadAxis>(axis);

  // Trigger axes map to analog buttons, not stick axes.
  if (IsTriggerAxis(sdl_axis)) {
    int button_index = TriggerAxisToButtonIndex(sdl_axis);
    if (button_index < 0) {
      return;
    }

    double normalized = NormalizeTriggerAxis(value);
    bool pressed = normalized > 0.5;

    flutter::EncodableMap event;
    event[flutter::EncodableValue("type")] =
        flutter::EncodableValue(std::string("button"));
    event[flutter::EncodableValue("gamepadId")] =
        flutter::EncodableValue(MakeGamepadId(joystick_id));
    event[flutter::EncodableValue("timestamp")] =
        flutter::EncodableValue(CurrentTimestamp());
    event[flutter::EncodableValue("button")] =
        flutter::EncodableValue(static_cast<int32_t>(button_index));
    event[flutter::EncodableValue("pressed")] =
        flutter::EncodableValue(pressed);
    event[flutter::EncodableValue("value")] =
        flutter::EncodableValue(normalized);

    stream_handler_->SendEvent(flutter::EncodableValue(event));
    return;
  }

  // Regular stick axis.
  int w3c_index = SdlAxisToW3C(sdl_axis);
  if (w3c_index < 0) {
    return;
  }

  double normalized = NormalizeStickAxis(value);

  flutter::EncodableMap event;
  event[flutter::EncodableValue("type")] =
      flutter::EncodableValue(std::string("axis"));
  event[flutter::EncodableValue("gamepadId")] =
      flutter::EncodableValue(MakeGamepadId(joystick_id));
  event[flutter::EncodableValue("timestamp")] =
      flutter::EncodableValue(CurrentTimestamp());
  event[flutter::EncodableValue("axis")] =
      flutter::EncodableValue(static_cast<int32_t>(w3c_index));
  event[flutter::EncodableValue("value")] =
      flutter::EncodableValue(normalized);

  stream_handler_->SendEvent(flutter::EncodableValue(event));
}

std::string SdlManager::MakeGamepadId(SDL_JoystickID id) {
  return "win_" + std::to_string(id);
}

int64_t SdlManager::CurrentTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();
  return static_cast<int64_t>(ms);
}

}  // namespace gamepad
