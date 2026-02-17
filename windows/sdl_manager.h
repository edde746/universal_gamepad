#ifndef FLUTTER_PLUGIN_SDL_MANAGER_H_
#define FLUTTER_PLUGIN_SDL_MANAGER_H_

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <flutter/encodable_value.h>

#include "button_mapping.h"

namespace gamepad {

class GamepadStreamHandler;

/// Manages SDL3 gamepad lifecycle and event polling on a background thread.
///
/// Detects gamepad connection/disconnection, button presses, and axis motion
/// for any controller supported by SDL3's built-in gamepad database.
/// Emits events through a GamepadStreamHandler.
class SdlManager {
 public:
  /// Axis values that change by less than this threshold are suppressed.
  static constexpr double kAxisEpsilon = 0.005;

  explicit SdlManager(std::shared_ptr<GamepadStreamHandler> stream_handler);
  ~SdlManager();

  /// Begins polling on a background thread.
  void StartPolling();

  /// Stops polling and joins the background thread.
  void StopPolling();

  /// Returns a list of currently connected gamepads as EncodableList.
  /// Each element is an EncodableMap with keys: id, name, vendorId, productId.
  flutter::EncodableList ListGamepads();

 private:
  /// Information cached for each connected gamepad.
  struct GamepadInfo {
    SDL_JoystickID joystick_id;
    SDL_Gamepad* gamepad;
    std::string name;
    uint16_t vendor_id;
    uint16_t product_id;
    /// Last emitted stick axis values for throttling (indexed by W3C axis 0-3).
    double last_axis[4];
    /// Last emitted trigger values for throttling (0 = L2, 1 = R2).
    double last_trigger[2];
  };

  /// Main polling loop, runs on background thread.
  void PollLoop();

  /// Processes all pending SDL events.
  void PollEvents();

  /// Handles a gamepad added event.
  void HandleGamepadAdded(SDL_JoystickID joystick_id);

  /// Handles a gamepad removed event.
  void HandleGamepadRemoved(SDL_JoystickID joystick_id);

  /// Handles a gamepad button event.
  void HandleButtonEvent(SDL_JoystickID joystick_id, uint8_t button,
                         bool pressed);

  /// Handles a gamepad axis event.
  void HandleAxisEvent(SDL_JoystickID joystick_id, uint8_t axis,
                       int16_t value);

  /// Returns the current timestamp in milliseconds since epoch.
  static int64_t CurrentTimestamp();

  std::shared_ptr<GamepadStreamHandler> stream_handler_;

  std::thread poll_thread_;
  std::atomic<bool> running_{false};

  /// Protects gamepads_ for cross-thread access from ListGamepads().
  std::mutex state_mutex_;

  /// Map of joystick ID to connected gamepad info.
  std::unordered_map<SDL_JoystickID, GamepadInfo> gamepads_;
};

}  // namespace gamepad

#endif  // FLUTTER_PLUGIN_SDL_MANAGER_H_
