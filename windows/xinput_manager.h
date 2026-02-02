#ifndef FLUTTER_PLUGIN_XINPUT_MANAGER_H_
#define FLUTTER_PLUGIN_XINPUT_MANAGER_H_

#include <windows.h>
#include <xinput.h>

#include <atomic>
#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <flutter/encodable_value.h>

#include "button_mapping.h"

namespace gamepad {

class GamepadStreamHandler;

/// Per-gamepad state tracked between polls.
struct GamepadState {
  bool connected = false;
  XINPUT_STATE xinput_state = {};
  /// Cached normalized trigger values to detect changes.
  double left_trigger = 0.0;
  double right_trigger = 0.0;
  /// Cached normalized axis values to detect changes.
  double axes[W3CAxis::kCount] = {0.0, 0.0, 0.0, 0.0};
};

/// Manages XInput polling on a background thread.
///
/// Polls up to XUSER_MAX_COUNT (4) gamepads at approximately 60 Hz.
/// Detects connection/disconnection, button state changes, and axis/trigger
/// value changes. Emits events through a GamepadStreamHandler.
class XInputManager {
 public:
  /// Constructs the manager. Does not start polling until StartPolling() is
  /// called.
  explicit XInputManager(std::shared_ptr<GamepadStreamHandler> stream_handler);
  ~XInputManager();

  /// Begins polling on a background thread.
  void StartPolling();

  /// Stops polling and joins the background thread.
  void StopPolling();

  /// Returns a list of currently connected gamepads as EncodableList.
  /// Each element is an EncodableMap with keys: id, name, vendorId, productId.
  flutter::EncodableList ListGamepads();

 private:
  /// Main polling loop, runs on background thread.
  void PollLoop();

  /// Processes a single gamepad slot.
  void ProcessGamepad(DWORD user_index);

  /// Emits a connection event.
  void EmitConnectionEvent(DWORD user_index, bool connected);

  /// Emits a button event.
  void EmitButtonEvent(DWORD user_index, int w3c_button, bool pressed,
                       double value);

  /// Emits an axis event.
  void EmitAxisEvent(DWORD user_index, int w3c_axis, double value);

  /// Returns a gamepad ID string for the given user index.
  static std::string MakeGamepadId(DWORD user_index);

  /// Returns the current timestamp in milliseconds since epoch.
  static int64_t CurrentTimestamp();

  std::shared_ptr<GamepadStreamHandler> stream_handler_;

  std::thread poll_thread_;
  std::atomic<bool> running_{false};

  std::mutex state_mutex_;
  std::array<GamepadState, XUSER_MAX_COUNT> gamepad_states_;
};

}  // namespace gamepad

#endif  // FLUTTER_PLUGIN_XINPUT_MANAGER_H_
