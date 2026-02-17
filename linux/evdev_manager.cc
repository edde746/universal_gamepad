#include "evdev_manager.h"

#include "button_mapping.h"

#include <chrono>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

EvdevManager::EvdevManager() = default;

EvdevManager::~EvdevManager() { Stop(); }

void EvdevManager::Start(EventCallback callback) {
  callback_ = std::move(callback);

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
}

void EvdevManager::Stop() {
  // Remove directory monitor.
  if (dir_monitor_) {
    if (dir_monitor_signal_id_) {
      g_signal_handler_disconnect(dir_monitor_, dir_monitor_signal_id_);
      dir_monitor_signal_id_ = 0;
    }
    g_file_monitor_cancel(dir_monitor_);
    g_object_unref(dir_monitor_);
    dir_monitor_ = nullptr;
  }

  // Close all devices.
  for (auto& [path, info] : devices_) {
    if (info.io_watch_id) {
      g_source_remove(info.io_watch_id);
    }
    libevdev_free(info.evdev);
    close(info.fd);
  }
  devices_.clear();

  callback_ = nullptr;
}

FlValue* EvdevManager::ListGamepads() {
  FlValue* list = fl_value_new_list();

  for (const auto& [path, info] : devices_) {
    FlValue* map = fl_value_new_map();
    fl_value_set_string_take(map, "id",
                             fl_value_new_string(info.id.c_str()));
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
  if (!callback_) return;

  for (const auto& [path, info] : devices_) {
    int64_t ts = NowMillis();
    FlValue* event = fl_value_new_map();
    fl_value_set_string_take(event, "type",
                             fl_value_new_string("connection"));
    fl_value_set_string_take(event, "gamepadId",
                             fl_value_new_string(info.id.c_str()));
    fl_value_set_string_take(event, "timestamp", fl_value_new_int(ts));
    fl_value_set_string_take(event, "connected", fl_value_new_bool(TRUE));
    fl_value_set_string_take(event, "name",
                             fl_value_new_string(info.name.c_str()));
    fl_value_set_string_take(event, "vendorId",
                             fl_value_new_int(info.vendor_id));
    fl_value_set_string_take(event, "productId",
                             fl_value_new_int(info.product_id));
    callback_(event);
    fl_value_unref(event);
  }
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

int64_t EvdevManager::NowMillis() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();
  return static_cast<int64_t>(ms);
}

bool EvdevManager::IsGamepad(struct libevdev* dev) {
  // Same heuristic as systemd/libmanette: has gamepad buttons or joystick axes.
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
  // Skip if already tracked.
  if (devices_.count(path)) return;

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
  info.id = "linux_" + std::to_string(next_id_++);

  const char* name = libevdev_get_name(dev);
  info.name = name ? name : "Unknown Gamepad";
  info.vendor_id = static_cast<uint16_t>(libevdev_get_id_vendor(dev));
  info.product_id = static_cast<uint16_t>(libevdev_get_id_product(dev));

  // Cache abs_info for axis normalization.
  for (unsigned int code = 0; code < ABS_MAX; ++code) {
    if (libevdev_has_event_code(dev, EV_ABS, code)) {
      const struct input_absinfo* ai = libevdev_get_abs_info(dev, code);
      if (ai) {
        info.abs_info[code] = *ai;
      }
    }
  }

  // Set up GLib IO watch.
  GIOChannel* channel = g_io_channel_unix_new(fd);
  std::string path_str(path);
  info.io_watch_id = g_io_add_watch(
      channel, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
      [](GIOChannel* source, GIOCondition condition,
         gpointer user_data) -> gboolean {
        auto* self = static_cast<EvdevManager*>(user_data);

        int fd = g_io_channel_unix_get_fd(source);
        // Find the device by fd.
        for (auto& [p, d] : self->devices_) {
          if (d.fd == fd) {
            if (condition & (G_IO_HUP | G_IO_ERR)) {
              // Device disconnected — schedule removal on next idle to avoid
              // modifying the map during iteration.
              std::string* path_copy = new std::string(p);
              g_idle_add(
                  [](gpointer data) -> gboolean {
                    auto* args =
                        static_cast<std::pair<EvdevManager*, std::string*>*>(
                            data);
                    args->first->RemoveDevice(args->second->c_str());
                    delete args->second;
                    delete args;
                    return G_SOURCE_REMOVE;
                  },
                  new std::pair<EvdevManager*, std::string*>(self, path_copy));
              return G_SOURCE_REMOVE;
            }
            self->OnInput(d);
            return G_SOURCE_CONTINUE;
          }
        }
        return G_SOURCE_REMOVE;
      },
      this);
  g_io_channel_unref(channel);

  devices_[path_str] = std::move(info);

  // Emit connection event.
  if (callback_) {
    const auto& stored = devices_[path_str];
    int64_t ts = NowMillis();
    FlValue* event = fl_value_new_map();
    fl_value_set_string_take(event, "type",
                             fl_value_new_string("connection"));
    fl_value_set_string_take(event, "gamepadId",
                             fl_value_new_string(stored.id.c_str()));
    fl_value_set_string_take(event, "timestamp", fl_value_new_int(ts));
    fl_value_set_string_take(event, "connected", fl_value_new_bool(TRUE));
    fl_value_set_string_take(event, "name",
                             fl_value_new_string(stored.name.c_str()));
    fl_value_set_string_take(event, "vendorId",
                             fl_value_new_int(stored.vendor_id));
    fl_value_set_string_take(event, "productId",
                             fl_value_new_int(stored.product_id));
    callback_(event);
    fl_value_unref(event);
  }
}

void EvdevManager::RemoveDevice(const char* path) {
  auto it = devices_.find(path);
  if (it == devices_.end()) return;

  DeviceInfo info = std::move(it->second);
  devices_.erase(it);

  if (info.io_watch_id) {
    g_source_remove(info.io_watch_id);
  }
  libevdev_free(info.evdev);
  close(info.fd);

  // Emit disconnection event.
  if (callback_) {
    int64_t ts = NowMillis();
    FlValue* event = fl_value_new_map();
    fl_value_set_string_take(event, "type",
                             fl_value_new_string("connection"));
    fl_value_set_string_take(event, "gamepadId",
                             fl_value_new_string(info.id.c_str()));
    fl_value_set_string_take(event, "timestamp", fl_value_new_int(ts));
    fl_value_set_string_take(event, "connected", fl_value_new_bool(FALSE));
    fl_value_set_string_take(event, "name",
                             fl_value_new_string(info.name.c_str()));
    fl_value_set_string_take(event, "vendorId",
                             fl_value_new_int(info.vendor_id));
    fl_value_set_string_take(event, "productId",
                             fl_value_new_int(info.product_id));
    callback_(event);
    fl_value_unref(event);
  }
}

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
      FlValue* fe = fl_value_new_map();
      fl_value_set_string_take(fe, "type", fl_value_new_string("button"));
      fl_value_set_string_take(fe, "gamepadId",
                               fl_value_new_string(info.id.c_str()));
      fl_value_set_string_take(fe, "timestamp", fl_value_new_int(ts));
      fl_value_set_string_take(fe, "button", fl_value_new_int(w3c_index));
      fl_value_set_string_take(fe, "pressed",
                               fl_value_new_bool(pressed ? TRUE : FALSE));
      fl_value_set_string_take(fe, "value",
                               fl_value_new_float(pressed ? 1.0 : 0.0));
      if (callback_) {
        callback_(fe);
      }
      fl_value_unref(fe);

    } else if (ev.type == EV_ABS) {
      if (ButtonMapping::IsHatAxis(ev.code)) {
        // D-pad via hat axis.
        int64_t ts = NowMillis();
        const std::string& gid = info.id;

        if (ev.code == ABS_HAT0X) {
          {
            FlValue* fe = fl_value_new_map();
            fl_value_set_string_take(fe, "type",
                                     fl_value_new_string("button"));
            fl_value_set_string_take(
                fe, "gamepadId", fl_value_new_string(gid.c_str()));
            fl_value_set_string_take(fe, "timestamp", fl_value_new_int(ts));
            fl_value_set_string_take(
                fe, "button",
                fl_value_new_int(ButtonMapping::kDpadLeft));
            bool pressed = ev.value < 0;
            fl_value_set_string_take(
                fe, "pressed",
                fl_value_new_bool(pressed ? TRUE : FALSE));
            fl_value_set_string_take(
                fe, "value", fl_value_new_float(pressed ? 1.0 : 0.0));
            if (callback_) callback_(fe);
            fl_value_unref(fe);
          }
          {
            FlValue* fe = fl_value_new_map();
            fl_value_set_string_take(fe, "type",
                                     fl_value_new_string("button"));
            fl_value_set_string_take(
                fe, "gamepadId", fl_value_new_string(gid.c_str()));
            fl_value_set_string_take(fe, "timestamp", fl_value_new_int(ts));
            fl_value_set_string_take(
                fe, "button",
                fl_value_new_int(ButtonMapping::kDpadRight));
            bool pressed = ev.value > 0;
            fl_value_set_string_take(
                fe, "pressed",
                fl_value_new_bool(pressed ? TRUE : FALSE));
            fl_value_set_string_take(
                fe, "value", fl_value_new_float(pressed ? 1.0 : 0.0));
            if (callback_) callback_(fe);
            fl_value_unref(fe);
          }
        } else if (ev.code == ABS_HAT0Y) {
          {
            FlValue* fe = fl_value_new_map();
            fl_value_set_string_take(fe, "type",
                                     fl_value_new_string("button"));
            fl_value_set_string_take(
                fe, "gamepadId", fl_value_new_string(gid.c_str()));
            fl_value_set_string_take(fe, "timestamp", fl_value_new_int(ts));
            fl_value_set_string_take(
                fe, "button",
                fl_value_new_int(ButtonMapping::kDpadUp));
            bool pressed = ev.value < 0;
            fl_value_set_string_take(
                fe, "pressed",
                fl_value_new_bool(pressed ? TRUE : FALSE));
            fl_value_set_string_take(
                fe, "value", fl_value_new_float(pressed ? 1.0 : 0.0));
            if (callback_) callback_(fe);
            fl_value_unref(fe);
          }
          {
            FlValue* fe = fl_value_new_map();
            fl_value_set_string_take(fe, "type",
                                     fl_value_new_string("button"));
            fl_value_set_string_take(
                fe, "gamepadId", fl_value_new_string(gid.c_str()));
            fl_value_set_string_take(fe, "timestamp", fl_value_new_int(ts));
            fl_value_set_string_take(
                fe, "button",
                fl_value_new_int(ButtonMapping::kDpadDown));
            bool pressed = ev.value > 0;
            fl_value_set_string_take(
                fe, "pressed",
                fl_value_new_bool(pressed ? TRUE : FALSE));
            fl_value_set_string_take(
                fe, "value", fl_value_new_float(pressed ? 1.0 : 0.0));
            if (callback_) callback_(fe);
            fl_value_unref(fe);
          }
        }

      } else if (ButtonMapping::IsTriggerAxis(ev.code)) {
        // Trigger axes → emit as button events (0..1 range).
        int button_index = ButtonMapping::TriggerAxisToButtonIndex(ev.code);
        if (button_index < 0) continue;

        const struct input_absinfo& ai = info.abs_info[ev.code];
        double range = ai.maximum - ai.minimum;
        double value = (range != 0)
                           ? static_cast<double>(ev.value - ai.minimum) / range
                           : 0.0;
        bool pressed = value > 0.5;

        int64_t ts = NowMillis();
        FlValue* fe = fl_value_new_map();
        fl_value_set_string_take(fe, "type", fl_value_new_string("button"));
        fl_value_set_string_take(fe, "gamepadId",
                                 fl_value_new_string(info.id.c_str()));
        fl_value_set_string_take(fe, "timestamp", fl_value_new_int(ts));
        fl_value_set_string_take(fe, "button",
                                 fl_value_new_int(button_index));
        fl_value_set_string_take(fe, "pressed",
                                 fl_value_new_bool(pressed ? TRUE : FALSE));
        fl_value_set_string_take(fe, "value", fl_value_new_float(value));
        if (callback_) callback_(fe);
        fl_value_unref(fe);

      } else {
        // Regular stick axis → -1..1 range.
        int w3c_index = ButtonMapping::EvdevAxisToW3C(ev.code);
        if (w3c_index < 0) continue;

        const struct input_absinfo& ai = info.abs_info[ev.code];
        double range = ai.maximum - ai.minimum;
        double value = (range != 0)
                           ? 2.0 * (ev.value - ai.minimum) / range - 1.0
                           : 0.0;

        int64_t ts = NowMillis();
        FlValue* fe = fl_value_new_map();
        fl_value_set_string_take(fe, "type", fl_value_new_string("axis"));
        fl_value_set_string_take(fe, "gamepadId",
                                 fl_value_new_string(info.id.c_str()));
        fl_value_set_string_take(fe, "timestamp", fl_value_new_int(ts));
        fl_value_set_string_take(fe, "axis", fl_value_new_int(w3c_index));
        fl_value_set_string_take(fe, "value", fl_value_new_float(value));
        if (callback_) callback_(fe);
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

  // Only care about event* nodes.
  g_autofree gchar* basename = g_file_get_basename(file);
  if (!basename || strncmp(basename, "event", 5) != 0) return;

  if (event_type == G_FILE_MONITOR_EVENT_CREATED) {
    self->AddDevice(path);
  } else if (event_type == G_FILE_MONITOR_EVENT_DELETED) {
    self->RemoveDevice(path);
  }
}
