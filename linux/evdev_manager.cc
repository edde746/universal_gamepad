#include "evdev_manager.h"

#include "button_mapping.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <unordered_set>

EvdevManager::EvdevManager() = default;

EvdevManager::~EvdevManager() { Stop(); }

// ---------------------------------------------------------------------------
// Public API (called from main thread)
// ---------------------------------------------------------------------------

void EvdevManager::Start(EventCallback callback) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(callback);
  }

  // Create a private GMainContext + GMainLoop for the worker thread.
  worker_context_ = g_main_context_new();
  worker_loop_ = g_main_loop_new(worker_context_, FALSE);

  // Push our context as the thread-default so that g_file_monitor and
  // g_source_attach use it rather than the global default.
  g_main_context_push_thread_default(worker_context_);

  ScanDevices();

  // Watch /dev/input/ for hotplug.
  GFile* dir = g_file_new_for_path("/dev/input");
  GError* error = nullptr;
  dir_monitor_ = g_file_monitor_directory(dir, G_FILE_MONITOR_NONE,
                                           nullptr, &error);
  g_object_unref(dir);

  if (dir_monitor_) {
    dir_monitor_signal_id_ = g_signal_connect(
        dir_monitor_, "changed",
        G_CALLBACK(OnDirectoryChanged), this);
  } else {
    g_warning("evdev: failed to monitor /dev/input: %s",
              error ? error->message : "unknown");
    if (error) g_error_free(error);
  }

  g_main_context_pop_thread_default(worker_context_);

  // Periodic timer on the main thread drains queued events.
  // No cross-thread g_idle_add / g_main_context_wakeup — the worker just
  // pushes to the queue and this timer picks them up at ~60 Hz.
  drain_timer_ = g_timeout_source_new(16);
  g_source_set_callback(drain_timer_, DrainEvents, this, nullptr);
  g_source_attach(drain_timer_, nullptr);  // default (main) context

  // Start the worker thread — it will run worker_loop_.
  worker_thread_ = g_thread_new("evdev-worker", ThreadFunc, this);
}

void EvdevManager::Stop() {
  // Signal the worker loop to quit.
  if (worker_loop_) {
    g_main_loop_quit(worker_loop_);
  }
  if (worker_thread_) {
    g_thread_join(worker_thread_);
    worker_thread_ = nullptr;
  }

  // Now single-threaded — safe to clean up without locks.
  if (dir_monitor_) {
    if (dir_monitor_signal_id_) {
      g_signal_handler_disconnect(dir_monitor_, dir_monitor_signal_id_);
      dir_monitor_signal_id_ = 0;
    }
    g_file_monitor_cancel(dir_monitor_);
    g_object_unref(dir_monitor_);
    dir_monitor_ = nullptr;
  }

  for (auto& [path, info] : devices_) {
    if (info.io_source) {
      g_source_destroy(info.io_source);
      g_source_unref(info.io_source);
    }
    libevdev_free(info.evdev);
    close(info.fd);
  }
  devices_.clear();

  if (worker_loop_) {
    g_main_loop_unref(worker_loop_);
    worker_loop_ = nullptr;
  }
  if (worker_context_) {
    g_main_context_unref(worker_context_);
    worker_context_ = nullptr;
  }

  if (drain_timer_) {
    g_source_destroy(drain_timer_);
    g_source_unref(drain_timer_);
    drain_timer_ = nullptr;
  }

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    for (FlValue* ev : pending_events_) {
      fl_value_unref(ev);
    }
    pending_events_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = nullptr;
  }
}

FlValue* EvdevManager::ListGamepads() {
  std::lock_guard<std::mutex> lock(mutex_);
  FlValue* list = fl_value_new_list();

  for (const auto& [path, info] : devices_) {
    FlValue* map = fl_value_new_map();
    fl_value_set_string_take(map, "id",
                             fl_value_new_int(info.id));
    fl_value_set_string_take(map, "name",
                             fl_value_new_string(info.name.c_str()));
    fl_value_set_string_take(map, "vendorId",
                             fl_value_new_int(info.vendor_id));
    fl_value_set_string_take(map, "productId",
                             fl_value_new_int(info.product_id));
    fl_value_append_take(list, map);
  }

  return list;
}

void EvdevManager::EmitExistingDevices() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!callback_) return;

  for (const auto& [path, info] : devices_) {
    int64_t ts = NowMillis();
    // Wire format: [0, gamepadId, timestamp, connected, name, vendorId, productId]
    FlValue* event = fl_value_new_list();
    fl_value_append_take(event, fl_value_new_int(0));
    fl_value_append_take(event, fl_value_new_int(info.id));
    fl_value_append_take(event, fl_value_new_int(ts));
    fl_value_append_take(event, fl_value_new_bool(TRUE));
    fl_value_append_take(event, fl_value_new_string(info.name.c_str()));
    fl_value_append_take(event, fl_value_new_int(info.vendor_id));
    fl_value_append_take(event, fl_value_new_int(info.product_id));
    callback_(event);
    fl_value_unref(event);
  }
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

gpointer EvdevManager::ThreadFunc(gpointer user_data) {
  auto* self = static_cast<EvdevManager*>(user_data);
  g_main_context_push_thread_default(self->worker_context_);
  g_main_loop_run(self->worker_loop_);
  g_main_context_pop_thread_default(self->worker_context_);
  return nullptr;
}

// ---------------------------------------------------------------------------
// Event forwarding (worker → main thread)
// ---------------------------------------------------------------------------

void EvdevManager::ForwardEvent(FlValue* event) {
  fl_value_ref(event);
  std::lock_guard<std::mutex> lock(queue_mutex_);
  pending_events_.push_back(event);
}

gboolean EvdevManager::DrainEvents(gpointer user_data) {
  auto* self = static_cast<EvdevManager*>(user_data);

  std::vector<FlValue*> events;
  {
    std::lock_guard<std::mutex> lock(self->queue_mutex_);
    if (self->pending_events_.empty()) return G_SOURCE_CONTINUE;
    events.swap(self->pending_events_);
  }

  // Coalesce axis events: keep only the latest per (gamepadId, axisIndex).
  // Scan in reverse so the first occurrence we see is the newest.
  // Wire format for axis: [2, gamepadId, timestamp, axisIndex, value]
  std::unordered_set<uint64_t> seen_axes;
  for (int i = static_cast<int>(events.size()) - 1; i >= 0; --i) {
    FlValue* ev = events[i];
    if (fl_value_get_length(ev) >= 5 &&
        fl_value_get_int(fl_value_get_list_value(ev, 0)) == 2) {
      int64_t gid = fl_value_get_int(fl_value_get_list_value(ev, 1));
      int64_t axis = fl_value_get_int(fl_value_get_list_value(ev, 3));
      uint64_t key = (static_cast<uint64_t>(gid) << 8) | static_cast<uint64_t>(axis);
      if (!seen_axes.insert(key).second) {
        fl_value_unref(ev);
        events[i] = nullptr;
      }
    }
  }

  EventCallback cb;
  {
    std::lock_guard<std::mutex> lock(self->mutex_);
    cb = self->callback_;
  }

  for (FlValue* ev : events) {
    if (!ev) continue;
    if (cb) cb(ev);
    fl_value_unref(ev);
  }

  return G_SOURCE_CONTINUE;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

int64_t EvdevManager::NowMillis() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();
  return static_cast<int64_t>(ms);
}

bool EvdevManager::IsGamepad(struct libevdev* dev) {
  bool has_buttons = libevdev_has_event_code(dev, EV_KEY, BTN_A) ||
                     libevdev_has_event_code(dev, EV_KEY, BTN_TRIGGER) ||
                     libevdev_has_event_code(dev, EV_KEY, BTN_1);
  bool has_axes = libevdev_has_event_code(dev, EV_ABS, ABS_RX) ||
                  libevdev_has_event_code(dev, EV_ABS, ABS_RY) ||
                  libevdev_has_event_code(dev, EV_ABS, ABS_RZ) ||
                  libevdev_has_event_code(dev, EV_ABS, ABS_THROTTLE) ||
                  libevdev_has_event_code(dev, EV_ABS, ABS_RUDDER) ||
                  libevdev_has_event_code(dev, EV_ABS, ABS_WHEEL) ||
                  libevdev_has_event_code(dev, EV_ABS, ABS_GAS) ||
                  libevdev_has_event_code(dev, EV_ABS, ABS_BRAKE);
  return has_buttons || has_axes;
}

void EvdevManager::ScanDevices() {
  DIR* dir = opendir("/dev/input");
  if (!dir) return;

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strncmp(entry->d_name, "event", 5) != 0) continue;
    std::string path = std::string("/dev/input/") + entry->d_name;
    AddDevice(path.c_str());
  }
  closedir(dir);
}

void EvdevManager::AddDevice(const char* path) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (devices_.count(path)) return;
  }

  int fd = open(path, O_RDONLY | O_NONBLOCK);
  if (fd < 0) return;

  struct libevdev* dev = nullptr;
  int rc = libevdev_new_from_fd(fd, &dev);
  if (rc < 0) {
    close(fd);
    return;
  }

  if (!IsGamepad(dev)) {
    libevdev_free(dev);
    close(fd);
    return;
  }

  DeviceInfo info{};
  info.fd = fd;
  info.evdev = dev;
  info.id = next_id_++;

  const char* name = libevdev_get_name(dev);
  info.name = name ? name : "Unknown Gamepad";
  info.vendor_id = static_cast<uint16_t>(libevdev_get_id_vendor(dev));
  info.product_id = static_cast<uint16_t>(libevdev_get_id_product(dev));

  // Initialize last-emitted values to NaN so the first event always fires.
  for (int i = 0; i < 4; ++i) info.last_axis[i] = NAN;
  for (int i = 0; i < 2; ++i) info.last_trigger[i] = NAN;

  // Cache abs_info for axis normalization.
  for (unsigned int code = 0; code < ABS_MAX; ++code) {
    if (libevdev_has_event_code(dev, EV_ABS, code)) {
      const struct input_absinfo* ai = libevdev_get_abs_info(dev, code);
      if (ai) {
        info.abs_info[code] = *ai;
      }
    }
  }

  // Attach an IO source to the worker context.
  GIOChannel* channel = g_io_channel_unix_new(fd);
  std::string path_str(path);

  GSource* source = g_io_create_watch(
      channel, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR));
  g_io_channel_unref(channel);

  struct WatchData {
    EvdevManager* self;
    std::string path;
  };
  auto* wd = new WatchData{this, path_str};

  g_source_set_callback(
      source,
      reinterpret_cast<GSourceFunc>(static_cast<GIOFunc>(
          [](GIOChannel* src, GIOCondition condition,
             gpointer user_data) -> gboolean {
            auto* wd = static_cast<WatchData*>(user_data);
            auto* self = wd->self;

            // No mutex_ needed: all worker-thread callbacks (IO, AddDevice,
            // RemoveDevice) are serialized by the worker GMainLoop. The main
            // thread only reads devices_ under mutex_ in ListGamepads /
            // EmitExistingDevices, and concurrent reads are safe.
            auto it = self->devices_.find(wd->path);
            if (it == self->devices_.end()) return G_SOURCE_REMOVE;

            if (condition & (G_IO_HUP | G_IO_ERR)) {
              auto* path_copy = new std::string(wd->path);
              auto* mgr = self;
              // Schedule removal as idle on worker context.
              GSource* idle = g_idle_source_new();
              g_source_set_callback(
                  idle,
                  [](gpointer data) -> gboolean {
                    auto* args =
                        static_cast<std::pair<EvdevManager*, std::string*>*>(
                            data);
                    args->first->RemoveDevice(args->second->c_str());
                    delete args->second;
                    delete args;
                    return G_SOURCE_REMOVE;
                  },
                  new std::pair<EvdevManager*, std::string*>(mgr, path_copy),
                  nullptr);
              g_source_attach(idle, self->worker_context_);
              g_source_unref(idle);
              return G_SOURCE_REMOVE;
            }

            self->OnInput(it->second);
            return G_SOURCE_CONTINUE;
          })),
      wd,
      [](gpointer data) { delete static_cast<WatchData*>(data); });

  g_source_attach(source, worker_context_);
  info.io_source = source;

  // Build connection event before inserting (we need the info fields).
  int64_t ts = NowMillis();
  // Wire format: [0, gamepadId, timestamp, connected, name, vendorId, productId]
  FlValue* event = fl_value_new_list();
  fl_value_append_take(event, fl_value_new_int(0));
  fl_value_append_take(event, fl_value_new_int(info.id));
  fl_value_append_take(event, fl_value_new_int(ts));
  fl_value_append_take(event, fl_value_new_bool(TRUE));
  fl_value_append_take(event, fl_value_new_string(info.name.c_str()));
  fl_value_append_take(event, fl_value_new_int(info.vendor_id));
  fl_value_append_take(event, fl_value_new_int(info.product_id));

  {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_[path_str] = std::move(info);
  }

  ForwardEvent(event);
  fl_value_unref(event);
}

void EvdevManager::RemoveDevice(const char* path) {
  DeviceInfo info;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(path);
    if (it == devices_.end()) return;
    info = std::move(it->second);
    devices_.erase(it);
  }

  if (info.io_source) {
    g_source_destroy(info.io_source);
    g_source_unref(info.io_source);
  }
  libevdev_free(info.evdev);
  close(info.fd);

  int64_t ts = NowMillis();
  // Wire format: [0, gamepadId, timestamp, connected, name, vendorId, productId]
  FlValue* event = fl_value_new_list();
  fl_value_append_take(event, fl_value_new_int(0));
  fl_value_append_take(event, fl_value_new_int(info.id));
  fl_value_append_take(event, fl_value_new_int(ts));
  fl_value_append_take(event, fl_value_new_bool(FALSE));
  fl_value_append_take(event, fl_value_new_string(info.name.c_str()));
  fl_value_append_take(event, fl_value_new_int(info.vendor_id));
  fl_value_append_take(event, fl_value_new_int(info.product_id));

  ForwardEvent(event);
  fl_value_unref(event);
}

// ---------------------------------------------------------------------------
// Event reading (runs on worker thread, mutex held by caller)
// ---------------------------------------------------------------------------

void EvdevManager::OnInput(DeviceInfo& info) {
  struct input_event ev;
  int rc;

  while ((rc = libevdev_next_event(info.evdev, LIBEVDEV_READ_FLAG_NORMAL,
                                    &ev)) == LIBEVDEV_READ_STATUS_SUCCESS) {
    if (ev.type == EV_KEY) {
      int w3c_index = ButtonMapping::EvdevButtonToW3C(ev.code);
      if (w3c_index < 0) continue;

      bool pressed = ev.value != 0;
      int64_t ts = NowMillis();
      // Wire format: [1, gamepadId, timestamp, buttonIndex, pressed, value]
      FlValue* fe = fl_value_new_list();
      fl_value_append_take(fe, fl_value_new_int(1));
      fl_value_append_take(fe, fl_value_new_int(info.id));
      fl_value_append_take(fe, fl_value_new_int(ts));
      fl_value_append_take(fe, fl_value_new_int(w3c_index));
      fl_value_append_take(fe, fl_value_new_bool(pressed ? TRUE : FALSE));
      fl_value_append_take(fe, fl_value_new_float(pressed ? 1.0 : 0.0));
      ForwardEvent(fe);
      fl_value_unref(fe);

    } else if (ev.type == EV_ABS) {
      if (ButtonMapping::IsHatAxis(ev.code)) {
        int64_t ts = NowMillis();
        int gid = info.id;

        if (ev.code == ABS_HAT0X) {
          {
            // Wire format: [1, gamepadId, timestamp, buttonIndex, pressed, value]
            FlValue* fe = fl_value_new_list();
            fl_value_append_take(fe, fl_value_new_int(1));
            fl_value_append_take(fe, fl_value_new_int(gid));
            fl_value_append_take(fe, fl_value_new_int(ts));
            fl_value_append_take(fe, fl_value_new_int(ButtonMapping::kDpadLeft));
            bool pressed = ev.value < 0;
            fl_value_append_take(fe, fl_value_new_bool(pressed ? TRUE : FALSE));
            fl_value_append_take(fe, fl_value_new_float(pressed ? 1.0 : 0.0));
            ForwardEvent(fe);
            fl_value_unref(fe);
          }
          {
            FlValue* fe = fl_value_new_list();
            fl_value_append_take(fe, fl_value_new_int(1));
            fl_value_append_take(fe, fl_value_new_int(gid));
            fl_value_append_take(fe, fl_value_new_int(ts));
            fl_value_append_take(fe, fl_value_new_int(ButtonMapping::kDpadRight));
            bool pressed = ev.value > 0;
            fl_value_append_take(fe, fl_value_new_bool(pressed ? TRUE : FALSE));
            fl_value_append_take(fe, fl_value_new_float(pressed ? 1.0 : 0.0));
            ForwardEvent(fe);
            fl_value_unref(fe);
          }
        } else if (ev.code == ABS_HAT0Y) {
          {
            FlValue* fe = fl_value_new_list();
            fl_value_append_take(fe, fl_value_new_int(1));
            fl_value_append_take(fe, fl_value_new_int(gid));
            fl_value_append_take(fe, fl_value_new_int(ts));
            fl_value_append_take(fe, fl_value_new_int(ButtonMapping::kDpadUp));
            bool pressed = ev.value < 0;
            fl_value_append_take(fe, fl_value_new_bool(pressed ? TRUE : FALSE));
            fl_value_append_take(fe, fl_value_new_float(pressed ? 1.0 : 0.0));
            ForwardEvent(fe);
            fl_value_unref(fe);
          }
          {
            FlValue* fe = fl_value_new_list();
            fl_value_append_take(fe, fl_value_new_int(1));
            fl_value_append_take(fe, fl_value_new_int(gid));
            fl_value_append_take(fe, fl_value_new_int(ts));
            fl_value_append_take(fe, fl_value_new_int(ButtonMapping::kDpadDown));
            bool pressed = ev.value > 0;
            fl_value_append_take(fe, fl_value_new_bool(pressed ? TRUE : FALSE));
            fl_value_append_take(fe, fl_value_new_float(pressed ? 1.0 : 0.0));
            ForwardEvent(fe);
            fl_value_unref(fe);
          }
        }

      } else if (ButtonMapping::IsTriggerAxis(ev.code)) {
        int button_index = ButtonMapping::TriggerAxisToButtonIndex(ev.code);
        if (button_index < 0) continue;

        const struct input_absinfo& ai = info.abs_info[ev.code];
        double range = ai.maximum - ai.minimum;
        double value = (range != 0)
                           ? static_cast<double>(ev.value - ai.minimum) / range
                           : 0.0;

        // Throttle: skip if value hasn't changed meaningfully.
        int trigger_idx = (ev.code == ABS_Z) ? 0 : 1;
        if (!std::isnan(info.last_trigger[trigger_idx]) &&
            std::fabs(value - info.last_trigger[trigger_idx]) < kAxisEpsilon) {
          continue;
        }
        info.last_trigger[trigger_idx] = value;

        bool pressed = value > 0.5;
        int64_t ts = NowMillis();
        // Wire format: [1, gamepadId, timestamp, buttonIndex, pressed, value]
        FlValue* fe = fl_value_new_list();
        fl_value_append_take(fe, fl_value_new_int(1));
        fl_value_append_take(fe, fl_value_new_int(info.id));
        fl_value_append_take(fe, fl_value_new_int(ts));
        fl_value_append_take(fe, fl_value_new_int(button_index));
        fl_value_append_take(fe, fl_value_new_bool(pressed ? TRUE : FALSE));
        fl_value_append_take(fe, fl_value_new_float(value));
        ForwardEvent(fe);
        fl_value_unref(fe);

      } else {
        int w3c_index = ButtonMapping::EvdevAxisToW3C(ev.code);
        if (w3c_index < 0) continue;

        const struct input_absinfo& ai = info.abs_info[ev.code];
        double range = ai.maximum - ai.minimum;
        double value = (range != 0)
                           ? 2.0 * (ev.value - ai.minimum) / range - 1.0
                           : 0.0;

        // Throttle: skip if value hasn't changed meaningfully.
        if (w3c_index < 4 &&
            !std::isnan(info.last_axis[w3c_index]) &&
            std::fabs(value - info.last_axis[w3c_index]) < kAxisEpsilon) {
          continue;
        }
        if (w3c_index < 4) info.last_axis[w3c_index] = value;

        int64_t ts = NowMillis();
        // Wire format: [2, gamepadId, timestamp, axisIndex, value]
        FlValue* fe = fl_value_new_list();
        fl_value_append_take(fe, fl_value_new_int(2));
        fl_value_append_take(fe, fl_value_new_int(info.id));
        fl_value_append_take(fe, fl_value_new_int(ts));
        fl_value_append_take(fe, fl_value_new_int(w3c_index));
        fl_value_append_take(fe, fl_value_new_float(value));
        ForwardEvent(fe);
        fl_value_unref(fe);
      }
    }
  }

  // Handle SYN_DROPPED: re-sync the device.
  if (rc == LIBEVDEV_READ_STATUS_SYNC) {
    while ((rc = libevdev_next_event(info.evdev, LIBEVDEV_READ_FLAG_SYNC,
                                      &ev)) == LIBEVDEV_READ_STATUS_SYNC) {
      // Drain sync events.
    }
  }
}

void EvdevManager::OnDirectoryChanged(GFileMonitor* monitor, GFile* file,
                                       GFile* other,
                                       GFileMonitorEvent event_type,
                                       gpointer user_data) {
  auto* self = static_cast<EvdevManager*>(user_data);

  g_autofree gchar* path = g_file_get_path(file);
  if (!path) return;

  g_autofree gchar* basename = g_file_get_basename(file);
  if (!basename || strncmp(basename, "event", 5) != 0) return;

  if (event_type == G_FILE_MONITOR_EVENT_CREATED) {
    self->AddDevice(path);
  } else if (event_type == G_FILE_MONITOR_EVENT_DELETED) {
    self->RemoveDevice(path);
  }
}
