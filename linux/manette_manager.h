#ifndef MANETTE_MANAGER_H_
#define MANETTE_MANAGER_H_

#include <flutter_linux/flutter_linux.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#if HAVE_MANETTE
#include <libmanette.h>
#endif

/// Manages gamepad lifecycle via libmanette on the GLib main loop.
///
/// All events fire as GLib signals on the main thread — no polling, no
/// background thread, no mutex. ManetteMonitor watches udev for hotplug,
/// ManetteDevice signals deliver button/axis events.
class ManetteManager {
 public:
  /// Callback type: receives an owned FlValue* map for each event.
  /// The callee is responsible for unreffing the value.
  using EventCallback = std::function<void(FlValue* event)>;

  ManetteManager();
  ~ManetteManager();

  /// Starts monitoring for gamepad events. Events are delivered via
  /// |callback| on the GLib main thread.
  void Start(EventCallback callback);

  /// Stops monitoring, disconnects all signal handlers.
  void Stop();

  /// Returns a list (FlValue array) of currently connected gamepads.
  /// Caller owns the returned FlValue*.
  FlValue* ListGamepads();

 private:
#if HAVE_MANETTE
  struct DeviceInfo {
    ManetteDevice* device;
    std::string id;
    std::string name;
    uint16_t vendor_id;
    uint16_t product_id;
    std::vector<gulong> signal_ids;
  };

  /// Returns the current timestamp in milliseconds since epoch.
  static int64_t NowMillis();

  /// Parses vendor and product IDs from the SDL GUID string returned by
  /// manette_device_get_guid(). The GUID is a 32-char hex string with
  /// little-endian fields; vendor is at byte offset 8 and product at 16.
  static void ParseGuid(const char* guid, uint16_t* vendor, uint16_t* product);

  /// Registers per-device signal handlers and emits a connection event.
  void AddDevice(ManetteDevice* device);

  /// Disconnects per-device signal handlers and emits a disconnection event.
  void RemoveDevice(ManetteDevice* device);

  // GLib signal callbacks (static C-style trampolines).
  static void OnDeviceConnected(ManetteMonitor* monitor,
                                ManetteDevice* device,
                                gpointer user_data);
  static void OnDeviceDisconnected(ManetteMonitor* monitor,
                                   ManetteDevice* device,
                                   gpointer user_data);
  static void OnButtonPress(ManetteDevice* device,
                            ManetteEvent* event,
                            gpointer user_data);
  static void OnButtonRelease(ManetteDevice* device,
                              ManetteEvent* event,
                              gpointer user_data);
  static void OnAbsoluteAxis(ManetteDevice* device,
                             ManetteEvent* event,
                             gpointer user_data);
  static void OnHatAxis(ManetteDevice* device,
                        ManetteEvent* event,
                        gpointer user_data);

  ManetteMonitor* monitor_ = nullptr;
  gulong monitor_connected_id_ = 0;
  gulong monitor_disconnected_id_ = 0;

  std::unordered_map<ManetteDevice*, DeviceInfo> devices_;
  int next_id_ = 0;
#endif  // HAVE_MANETTE

  EventCallback callback_;
};

#endif  // MANETTE_MANAGER_H_
