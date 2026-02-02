#include "xinput_manager.h"

#include "gamepad_stream_handler.h"

#include <cmath>

namespace gamepad {

// Threshold for floating-point axis change detection.
static constexpr double kAxisEpsilon = 0.001;

// Polling interval: ~60 Hz => ~16 ms.
static constexpr auto kPollInterval = std::chrono::milliseconds(16);

XInputManager::XInputManager(
    std::shared_ptr<GamepadStreamHandler> stream_handler)
    : stream_handler_(std::move(stream_handler)) {}

XInputManager::~XInputManager() { StopPolling(); }

void XInputManager::StartPolling() {
  if (running_.load()) return;
  running_.store(true);
  poll_thread_ = std::thread(&XInputManager::PollLoop, this);
}

void XInputManager::StopPolling() {
  running_.store(false);
  if (poll_thread_.joinable()) {
    poll_thread_.join();
  }
}

flutter::EncodableList XInputManager::ListGamepads() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  flutter::EncodableList result;

  for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
    if (gamepad_states_[i].connected) {
      flutter::EncodableMap info;
      info[flutter::EncodableValue("id")] =
          flutter::EncodableValue(MakeGamepadId(i));
      info[flutter::EncodableValue("name")] =
          flutter::EncodableValue(std::string("Xbox Controller"));
      info[flutter::EncodableValue("vendorId")] =
          flutter::EncodableValue(static_cast<int32_t>(0x045E));  // Microsoft
      info[flutter::EncodableValue("productId")] =
          flutter::EncodableValue(static_cast<int32_t>(0x02E0));  // Xbox One
      result.push_back(flutter::EncodableValue(info));
    }
  }
  return result;
}

void XInputManager::PollLoop() {
  while (running_.load()) {
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
      ProcessGamepad(i);
    }
    std::this_thread::sleep_for(kPollInterval);
  }
}

void XInputManager::ProcessGamepad(DWORD user_index) {
  XINPUT_STATE new_state;
  ZeroMemory(&new_state, sizeof(XINPUT_STATE));

  DWORD result = XInputGetState(user_index, &new_state);
  bool now_connected = (result == ERROR_SUCCESS);

  std::lock_guard<std::mutex> lock(state_mutex_);
  GamepadState& state = gamepad_states_[user_index];

  // --- Connection / disconnection ---
  if (now_connected && !state.connected) {
    state.connected = true;
    ZeroMemory(&state.xinput_state, sizeof(XINPUT_STATE));
    state.left_trigger = 0.0;
    state.right_trigger = 0.0;
    for (int a = 0; a < W3CAxis::kCount; ++a) {
      state.axes[a] = 0.0;
    }
    EmitConnectionEvent(user_index, true);
  } else if (!now_connected && state.connected) {
    state.connected = false;
    ZeroMemory(&state.xinput_state, sizeof(XINPUT_STATE));
    EmitConnectionEvent(user_index, false);
    return;
  }

  if (!now_connected) return;

  // Only process if the packet number changed (state was updated).
  if (new_state.dwPacketNumber == state.xinput_state.dwPacketNumber) return;

  const XINPUT_GAMEPAD& old_gp = state.xinput_state.Gamepad;
  const XINPUT_GAMEPAD& new_gp = new_state.Gamepad;

  // --- Digital buttons ---
  WORD old_buttons = old_gp.wButtons;
  WORD new_buttons = new_gp.wButtons;

  if (old_buttons != new_buttons) {
    for (WORD btn : GetAllXInputDigitalButtons()) {
      bool was_pressed = (old_buttons & btn) != 0;
      bool is_pressed = (new_buttons & btn) != 0;

      if (was_pressed != is_pressed) {
        int w3c = XInputButtonToW3C(btn);
        if (w3c >= 0) {
          EmitButtonEvent(user_index, w3c, is_pressed,
                          is_pressed ? 1.0 : 0.0);
        }
      }
    }
  }

  // --- Triggers (analog buttons) ---
  double new_lt =
      NormalizeTrigger(new_gp.bLeftTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
  if (std::abs(new_lt - state.left_trigger) > kAxisEpsilon) {
    state.left_trigger = new_lt;
    bool pressed = new_lt > 0.0;
    EmitButtonEvent(user_index, W3CButton::kLeftTrigger, pressed, new_lt);
  }

  double new_rt =
      NormalizeTrigger(new_gp.bRightTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
  if (std::abs(new_rt - state.right_trigger) > kAxisEpsilon) {
    state.right_trigger = new_rt;
    bool pressed = new_rt > 0.0;
    EmitButtonEvent(user_index, W3CButton::kRightTrigger, pressed, new_rt);
  }

  // --- Thumbstick axes ---
  double new_axes[W3CAxis::kCount];
  new_axes[W3CAxis::kLeftStickX] =
      NormalizeThumbstick(new_gp.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
  new_axes[W3CAxis::kLeftStickY] =
      NormalizeThumbstick(new_gp.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
  new_axes[W3CAxis::kRightStickX] = NormalizeThumbstick(
      new_gp.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
  new_axes[W3CAxis::kRightStickY] = NormalizeThumbstick(
      new_gp.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

  // W3C convention: Y axis positive = down, but XInput Y positive = up.
  // Invert Y axes to match W3C.
  new_axes[W3CAxis::kLeftStickY] = -new_axes[W3CAxis::kLeftStickY];
  new_axes[W3CAxis::kRightStickY] = -new_axes[W3CAxis::kRightStickY];

  for (int a = 0; a < W3CAxis::kCount; ++a) {
    if (std::abs(new_axes[a] - state.axes[a]) > kAxisEpsilon) {
      state.axes[a] = new_axes[a];
      EmitAxisEvent(user_index, a, new_axes[a]);
    }
  }

  // Update stored state.
  state.xinput_state = new_state;
}

void XInputManager::EmitConnectionEvent(DWORD user_index, bool connected) {
  flutter::EncodableMap event;
  event[flutter::EncodableValue("type")] =
      flutter::EncodableValue(std::string("connection"));
  event[flutter::EncodableValue("gamepadId")] =
      flutter::EncodableValue(MakeGamepadId(user_index));
  event[flutter::EncodableValue("timestamp")] =
      flutter::EncodableValue(CurrentTimestamp());
  event[flutter::EncodableValue("connected")] =
      flutter::EncodableValue(connected);
  event[flutter::EncodableValue("name")] =
      flutter::EncodableValue(std::string("Xbox Controller"));
  event[flutter::EncodableValue("vendorId")] =
      flutter::EncodableValue(static_cast<int32_t>(0x045E));  // Microsoft
  event[flutter::EncodableValue("productId")] =
      flutter::EncodableValue(static_cast<int32_t>(0x02E0));  // Xbox One

  stream_handler_->SendEvent(flutter::EncodableValue(event));
}

void XInputManager::EmitButtonEvent(DWORD user_index, int w3c_button,
                                    bool pressed, double value) {
  flutter::EncodableMap event;
  event[flutter::EncodableValue("type")] =
      flutter::EncodableValue(std::string("button"));
  event[flutter::EncodableValue("gamepadId")] =
      flutter::EncodableValue(MakeGamepadId(user_index));
  event[flutter::EncodableValue("timestamp")] =
      flutter::EncodableValue(CurrentTimestamp());
  event[flutter::EncodableValue("button")] =
      flutter::EncodableValue(static_cast<int32_t>(w3c_button));
  event[flutter::EncodableValue("pressed")] =
      flutter::EncodableValue(pressed);
  event[flutter::EncodableValue("value")] = flutter::EncodableValue(value);

  stream_handler_->SendEvent(flutter::EncodableValue(event));
}

void XInputManager::EmitAxisEvent(DWORD user_index, int w3c_axis,
                                  double value) {
  flutter::EncodableMap event;
  event[flutter::EncodableValue("type")] =
      flutter::EncodableValue(std::string("axis"));
  event[flutter::EncodableValue("gamepadId")] =
      flutter::EncodableValue(MakeGamepadId(user_index));
  event[flutter::EncodableValue("timestamp")] =
      flutter::EncodableValue(CurrentTimestamp());
  event[flutter::EncodableValue("axis")] =
      flutter::EncodableValue(static_cast<int32_t>(w3c_axis));
  event[flutter::EncodableValue("value")] = flutter::EncodableValue(value);

  stream_handler_->SendEvent(flutter::EncodableValue(event));
}

std::string XInputManager::MakeGamepadId(DWORD user_index) {
  return "win_" + std::to_string(user_index);
}

int64_t XInputManager::CurrentTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();
  return static_cast<int64_t>(ms);
}

}  // namespace gamepad
