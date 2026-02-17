#ifndef EVDEV_MANAGER_H_
#define EVDEV_MANAGER_H_

#include <flutter_linux/flutter_linux.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

/// Manages gamepad lifecycle via direct evdev + GFileMonitor hotplug.
///
/// All events fire on the GLib main thread via IO watches — no polling,
/// no background thread, no mutex.
class EvdevManager {
 public:
  /// Callback type: receives an owned FlValue* map for each event.
  /// The callee is responsible for unreffing the value.
  using EventCallback = std::function<void(FlValue* event)>;

  EvdevManager();
  ~EvdevManager();

  /// Starts monitoring for gamepad events. Events are delivered via
  /// |callback| on the GLib main thread.
  void Start(EventCallback callback);

  /// Stops monitoring, closes all devices.
  void Stop();

  /// Returns a list (FlValue array) of currently connected gamepads.
  /// Caller owns the returned FlValue*.
  FlValue* ListGamepads();

  /// Emits a connection event for each already-connected device via the
  /// current callback.
  void EmitExistingDevices();

 private:
  struct DeviceInfo {
    int fd;
    struct libevdev* evdev;
    std::string id;
    std::string name;
    uint16_t vendor_id;
    uint16_t product_id;
    struct input_absinfo abs_info[ABS_MAX];
    guint io_watch_id;
  };

  static int64_t NowMillis();
  bool IsGamepad(struct libevdev* dev);
  void ScanDevices();
  void AddDevice(const char* path);
  void RemoveDevice(const char* path);
  void OnInput(DeviceInfo& info);

  static void OnDirectoryChanged(GFileMonitor* monitor, GFile* file,
                                  GFile* other, GFileMonitorEvent event_type,
                                  gpointer user_data);

  GFileMonitor* dir_monitor_ = nullptr;
  gulong dir_monitor_signal_id_ = 0;
  std::unordered_map<std::string, DeviceInfo> devices_;  // keyed by path
  int next_id_ = 0;
  EventCallback callback_;
};

#endif  // EVDEV_MANAGER_H_
