#ifndef SDL_MANAGER_H_
#define SDL_MANAGER_H_

#include <flutter_linux/flutter_linux.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#if HAVE_SDL3
#include <SDL3/SDL.h>
#endif

/// Manages SDL3 gamepad lifecycle and event polling on a background thread.
///
/// Initialises SDL with SDL_INIT_GAMEPAD on a dedicated poll thread that runs
/// at ~60 Hz. Gamepad connection, button, and axis events are marshalled back
/// to the GLib main thread via g_idle_add and forwarded to a caller-supplied
/// callback as FlValue maps matching the plugin wire format.
class SdlManager {
 public:
  /// Callback type: receives an owned FlValue* map for each event.
  /// The callee is responsible for unreffing the value.
  using EventCallback = std::function<void(FlValue* event)>;

  SdlManager();
  ~SdlManager();

  /// Starts SDL event polling on a background thread. Events are delivered via
  /// |callback| on the GLib main thread.
  void Start(EventCallback callback);

  /// Stops event polling, joins the background thread, and shuts down SDL.
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

  /// Data passed through g_idle_add to dispatch an event on the main thread.
  /// Carries a shared_ptr to the callback so the idle callback remains safe
  /// even if the SdlManager has been destroyed by the time it fires.
  struct IdleEventData {
    FlValue* event;
    std::shared_ptr<EventCallback> callback;
  };

  /// Creates a gamepad ID string from a joystick ID (e.g. "linux_3").
  static std::string MakeGamepadId(SDL_JoystickID id);

  /// Returns the current timestamp in milliseconds since epoch.
  static int64_t NowMillis();

  /// Background thread entry point: inits SDL, polls in a loop, cleans up.
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
  void HandleAxisEvent(SDL_JoystickID joystick_id, uint8_t axis, int16_t value);

  /// Marshals an event to the main thread via g_idle_add.
  void DispatchEvent(FlValue* event);

  /// GLib idle callback that fires on the main thread.
  static gboolean IdleDispatchCallback(gpointer user_data);

  /// Map of joystick ID to connected gamepad info.
  std::unordered_map<SDL_JoystickID, GamepadInfo> gamepads_;

  /// Background poll thread.
  std::thread poll_thread_;

  /// Thread-safe stop flag.
  std::atomic<bool> running_{false};

  /// Protects gamepads_ for cross-thread access.
  std::mutex mutex_;
#endif  // HAVE_SDL3

  /// Event callback, shared with in-flight idle callbacks so they remain safe
  /// even after SdlManager destruction.
  std::shared_ptr<EventCallback> callback_;
};

#endif  // SDL_MANAGER_H_
