#ifndef SDL_MANAGER_H_
#define SDL_MANAGER_H_

#include <flutter_linux/flutter_linux.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#if HAVE_SDL3
#include <SDL3/SDL.h>
#endif

/// Manages SDL3 gamepad lifecycle and event polling.
///
/// Initialises SDL with SDL_INIT_GAMEPAD and polls for events on a GLib
/// timer (~16 ms, i.e. ~60 Hz). Gamepad connection, button, and axis events
/// are forwarded to a caller-supplied callback as FlValue maps matching the
/// plugin wire format.
class SdlManager {
 public:
  /// Callback type: receives an owned FlValue* map for each event.
  /// The callee is responsible for unreffing the value.
  using EventCallback = std::function<void(FlValue* event)>;

  SdlManager();
  ~SdlManager();

  /// Starts SDL initialisation and event polling. Events are delivered via
  /// |callback|, which must be callable from the GLib main thread.
  void Start(EventCallback callback);

  /// Stops event polling and shuts down SDL.
  void Stop();

  /// Returns a list (FlValue array) of currently connected gamepads.
  /// Caller owns the returned FlValue*.
  FlValue* ListGamepads();

 private:
#if HAVE_SDL3
  /// Information cached for each connected gamepad.
  struct GamepadInfo {
    SDL_JoystickID joystick_id;
    SDL_Gamepad* gamepad;
    std::string name;
    uint16_t vendor_id;
    uint16_t product_id;
  };

  /// Creates a gamepad ID string from a joystick ID (e.g. "linux_3").
  static std::string MakeGamepadId(SDL_JoystickID id);

  /// Returns the current timestamp in milliseconds since epoch.
  static int64_t NowMillis();

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
  void HandleAxisEvent(SDL_JoystickID joystick_id, uint8_t axis, int16_t value);

  /// GLib timeout callback (static C function).
  static gboolean PollCallback(gpointer user_data);

  /// Map of joystick ID to connected gamepad info.
  std::unordered_map<SDL_JoystickID, GamepadInfo> gamepads_;
#endif  // HAVE_SDL3

  /// Event callback.
  EventCallback callback_;

  /// GLib timeout source ID, or 0 if not polling.
  guint poll_source_id_ = 0;

  /// Whether SDL has been initialised.
  bool sdl_initialized_ = false;
};

#endif  // SDL_MANAGER_H_
