#ifndef EVDEV_MANAGER_H_
#define EVDEV_MANAGER_H_

#include <flutter_linux/flutter_linux.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/// Manages gamepad lifecycle via direct evdev on a dedicated GLib thread.
///
/// Device scanning, hotplug monitoring, and event reading all happen on a
/// private GMainLoop running in its own thread.  Finished events are queued
/// and drained by a 16 ms periodic timer on the main GMainContext so that
/// FlValue/EventChannel calls stay on the main thread.  No cross-thread
/// g_idle_add / g_main_context_wakeup is used — the worker just pushes to
/// the queue.
///
/// Axis events are throttled: a new value is only forwarded when it differs
/// from the previous value by more than kAxisEpsilon.  Duplicate axis events
/// in the same drain batch are coalesced to the latest value.
class EvdevManager {
 public:
  using EventCallback = std::function<void(FlValue* event)>;

  EvdevManager();
  ~EvdevManager();

  void Start(EventCallback callback);
  void Stop();
  FlValue* ListGamepads();
  void EmitExistingDevices();

 private:
  static constexpr double kAxisEpsilon = 0.005;

  struct DeviceInfo {
    int fd;
    struct libevdev* evdev;
    int id;
    std::string name;
    uint16_t vendor_id;
    uint16_t product_id;
    struct input_absinfo abs_info[ABS_MAX];
    GSource* io_source;
    // Last emitted axis values for throttling (indexed by W3C axis).
    double last_axis[4];
    // Last emitted trigger values for throttling (indexed by W3C button).
    double last_trigger[2];
  };

  static int64_t NowMillis();
  bool IsGamepad(struct libevdev* dev);
  void ScanDevices();
  void AddDevice(const char* path);
  void RemoveDevice(const char* path);
  void OnInput(DeviceInfo& info);

  /// Queue an event for delivery on the next timer tick.
  void ForwardEvent(FlValue* event);

  /// Main-thread timer callback that drains pending_events_.
  static gboolean DrainEvents(gpointer user_data);

  static void OnDirectoryChanged(GFileMonitor* monitor, GFile* file,
                                  GFile* other, GFileMonitorEvent event_type,
                                  gpointer user_data);
  static gpointer ThreadFunc(gpointer user_data);

  // Worker thread state — accessed only from the worker thread.
  GMainContext* worker_context_ = nullptr;
  GMainLoop* worker_loop_ = nullptr;
  GThread* worker_thread_ = nullptr;
  GFileMonitor* dir_monitor_ = nullptr;
  gulong dir_monitor_signal_id_ = 0;
  std::unordered_map<std::string, DeviceInfo> devices_;
  int next_id_ = 0;

  // Shared state — protected by mutex_.
  std::mutex mutex_;
  EventCallback callback_;

  // Event queue — protected by queue_mutex_.
  std::mutex queue_mutex_;
  std::vector<FlValue*> pending_events_;
  GSource* drain_timer_ = nullptr;
};

#endif  // EVDEV_MANAGER_H_
