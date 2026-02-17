#include "manette_manager.h"

#include "button_mapping.h"

#include <chrono>
#include <cstring>

ManetteManager::ManetteManager() = default;

ManetteManager::~ManetteManager() { Stop(); }

void ManetteManager::Start(EventCallback callback) {
  callback_ = std::move(callback);

#if HAVE_MANETTE
  if (monitor_) return;

  monitor_ = manette_monitor_new();

  monitor_connected_id_ = g_signal_connect(
      monitor_, "device-connected",
      G_CALLBACK(OnDeviceConnected), this);
  monitor_disconnected_id_ = g_signal_connect(
      monitor_, "device-disconnected",
      G_CALLBACK(OnDeviceDisconnected), this);

  // Enumerate already-connected devices.
  ManetteMonitorIter* iter = manette_monitor_iterate(monitor_);
  ManetteDevice* device = nullptr;
  while (manette_monitor_iter_next(iter, &device)) {
    AddDevice(device);
  }
  g_free(iter);
#else
  g_warning(
      "gamepad: libmanette not available. Gamepad support is disabled.");
#endif
}

void ManetteManager::Stop() {
#if HAVE_MANETTE
  // Disconnect all per-device signal handlers.
  for (auto& [dev, info] : devices_) {
    for (gulong id : info.signal_ids) {
      g_signal_handler_disconnect(dev, id);
    }
  }
  devices_.clear();

  // Disconnect monitor signals and release it.
  if (monitor_) {
    if (monitor_connected_id_) {
      g_signal_handler_disconnect(monitor_, monitor_connected_id_);
      monitor_connected_id_ = 0;
    }
    if (monitor_disconnected_id_) {
      g_signal_handler_disconnect(monitor_, monitor_disconnected_id_);
      monitor_disconnected_id_ = 0;
    }
    g_object_unref(monitor_);
    monitor_ = nullptr;
  }
#endif

  callback_ = nullptr;
}

FlValue* ManetteManager::ListGamepads() {
  FlValue* list = fl_value_new_list();

#if HAVE_MANETTE
  for (const auto& [dev, info] : devices_) {
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
#endif

  return list;
}

#if HAVE_MANETTE

int64_t ManetteManager::NowMillis() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();
  return static_cast<int64_t>(ms);
}

void ManetteManager::ParseGuid(const char* guid, uint16_t* vendor,
                                uint16_t* product) {
  *vendor = 0;
  *product = 0;

  if (!guid || strlen(guid) < 32) return;

  // SDL GUID format: 32 hex chars, little-endian 16-bit fields.
  // Bytes 8-9 (chars 16-19) = vendor ID, bytes 16-17 (chars 32... wait)
  // Actually the SDL GUID layout for Linux evdev:
  //   bytes 0-1: bus type (LE)
  //   bytes 2-3: 0
  //   bytes 4-5: vendor (LE)
  //   bytes 6-7: 0
  //   bytes 8-9: product (LE)
  //   bytes 10-11: 0
  //   bytes 12-15: version + driver
  // Each byte = 2 hex chars, so vendor at chars 8-11, product at chars 16-19.
  auto parse_le16 = [](const char* hex) -> uint16_t {
    auto nibble = [](char c) -> uint8_t {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
      if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
      return 0;
    };
    // Little-endian: first byte (2 chars) is low byte.
    uint8_t lo = (nibble(hex[0]) << 4) | nibble(hex[1]);
    uint8_t hi = (nibble(hex[2]) << 4) | nibble(hex[3]);
    return static_cast<uint16_t>((hi << 8) | lo);
  };

  *vendor = parse_le16(guid + 8);
  *product = parse_le16(guid + 16);
}

void ManetteManager::AddDevice(ManetteDevice* device) {
  if (devices_.count(device)) return;

  DeviceInfo info;
  info.device = device;
  info.id = "linux_" + std::to_string(next_id_++);

  const char* name = manette_device_get_name(device);
  info.name = name ? name : "Unknown Gamepad";

  const char* guid = manette_device_get_guid(device);
  ParseGuid(guid, &info.vendor_id, &info.product_id);

  // Connect per-device signals.
  info.signal_ids.push_back(g_signal_connect(
      device, "button-press-event",
      G_CALLBACK(OnButtonPress), this));
  info.signal_ids.push_back(g_signal_connect(
      device, "button-release-event",
      G_CALLBACK(OnButtonRelease), this));
  info.signal_ids.push_back(g_signal_connect(
      device, "absolute-axis-event",
      G_CALLBACK(OnAbsoluteAxis), this));
  info.signal_ids.push_back(g_signal_connect(
      device, "hat-axis-event",
      G_CALLBACK(OnHatAxis), this));

  devices_[device] = info;

  // Emit connection event.
  if (callback_) {
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

void ManetteManager::RemoveDevice(ManetteDevice* device) {
  auto it = devices_.find(device);
  if (it == devices_.end()) return;

  DeviceInfo info = it->second;

  // Disconnect per-device signal handlers.
  for (gulong id : info.signal_ids) {
    g_signal_handler_disconnect(device, id);
  }
  devices_.erase(it);

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

// -- GLib signal callbacks ---------------------------------------------------

void ManetteManager::OnDeviceConnected(ManetteMonitor* monitor,
                                       ManetteDevice* device,
                                       gpointer user_data) {
  auto* self = static_cast<ManetteManager*>(user_data);
  self->AddDevice(device);
}

void ManetteManager::OnDeviceDisconnected(ManetteMonitor* monitor,
                                          ManetteDevice* device,
                                          gpointer user_data) {
  auto* self = static_cast<ManetteManager*>(user_data);
  self->RemoveDevice(device);
}

void ManetteManager::OnButtonPress(ManetteDevice* device,
                                   ManetteEvent* event,
                                   gpointer user_data) {
  auto* self = static_cast<ManetteManager*>(user_data);
  if (!self->callback_) return;

  auto it = self->devices_.find(device);
  if (it == self->devices_.end()) return;

  uint16_t button;
  if (!manette_event_get_button(event, &button)) return;

  int w3c_index = ButtonMapping::EvdevButtonToW3C(button);
  if (w3c_index < 0) return;

  int64_t ts = NowMillis();
  FlValue* ev = fl_value_new_map();
  fl_value_set_string_take(ev, "type", fl_value_new_string("button"));
  fl_value_set_string_take(ev, "gamepadId",
                           fl_value_new_string(it->second.id.c_str()));
  fl_value_set_string_take(ev, "timestamp", fl_value_new_int(ts));
  fl_value_set_string_take(ev, "button", fl_value_new_int(w3c_index));
  fl_value_set_string_take(ev, "pressed", fl_value_new_bool(TRUE));
  fl_value_set_string_take(ev, "value", fl_value_new_float(1.0));
  self->callback_(ev);
  fl_value_unref(ev);
}

void ManetteManager::OnButtonRelease(ManetteDevice* device,
                                     ManetteEvent* event,
                                     gpointer user_data) {
  auto* self = static_cast<ManetteManager*>(user_data);
  if (!self->callback_) return;

  auto it = self->devices_.find(device);
  if (it == self->devices_.end()) return;

  uint16_t button;
  if (!manette_event_get_button(event, &button)) return;

  int w3c_index = ButtonMapping::EvdevButtonToW3C(button);
  if (w3c_index < 0) return;

  int64_t ts = NowMillis();
  FlValue* ev = fl_value_new_map();
  fl_value_set_string_take(ev, "type", fl_value_new_string("button"));
  fl_value_set_string_take(ev, "gamepadId",
                           fl_value_new_string(it->second.id.c_str()));
  fl_value_set_string_take(ev, "timestamp", fl_value_new_int(ts));
  fl_value_set_string_take(ev, "button", fl_value_new_int(w3c_index));
  fl_value_set_string_take(ev, "pressed", fl_value_new_bool(FALSE));
  fl_value_set_string_take(ev, "value", fl_value_new_float(0.0));
  self->callback_(ev);
  fl_value_unref(ev);
}

void ManetteManager::OnAbsoluteAxis(ManetteDevice* device,
                                    ManetteEvent* event,
                                    gpointer user_data) {
  auto* self = static_cast<ManetteManager*>(user_data);
  if (!self->callback_) return;

  auto it = self->devices_.find(device);
  if (it == self->devices_.end()) return;

  uint16_t axis;
  double value;
  if (!manette_event_get_absolute(event, &axis, &value)) return;

  // Trigger axes → emit as button events.
  if (ButtonMapping::IsTriggerAxis(axis)) {
    int button_index = ButtonMapping::TriggerAxisToButtonIndex(axis);
    if (button_index < 0) return;

    // value is already 0.0..1.0 from libmanette.
    bool pressed = value > 0.5;

    int64_t ts = NowMillis();
    FlValue* ev = fl_value_new_map();
    fl_value_set_string_take(ev, "type", fl_value_new_string("button"));
    fl_value_set_string_take(ev, "gamepadId",
                             fl_value_new_string(it->second.id.c_str()));
    fl_value_set_string_take(ev, "timestamp", fl_value_new_int(ts));
    fl_value_set_string_take(ev, "button",
                             fl_value_new_int(button_index));
    fl_value_set_string_take(ev, "pressed",
                             fl_value_new_bool(pressed ? TRUE : FALSE));
    fl_value_set_string_take(ev, "value", fl_value_new_float(value));
    self->callback_(ev);
    fl_value_unref(ev);
    return;
  }

  // Regular stick axis.
  int w3c_index = ButtonMapping::EvdevAxisToW3C(axis);
  if (w3c_index < 0) return;

  // value is already -1.0..1.0 from libmanette.
  int64_t ts = NowMillis();
  FlValue* ev = fl_value_new_map();
  fl_value_set_string_take(ev, "type", fl_value_new_string("axis"));
  fl_value_set_string_take(ev, "gamepadId",
                           fl_value_new_string(it->second.id.c_str()));
  fl_value_set_string_take(ev, "timestamp", fl_value_new_int(ts));
  fl_value_set_string_take(ev, "axis", fl_value_new_int(w3c_index));
  fl_value_set_string_take(ev, "value", fl_value_new_float(value));
  self->callback_(ev);
  fl_value_unref(ev);
}

void ManetteManager::OnHatAxis(ManetteDevice* device,
                               ManetteEvent* event,
                               gpointer user_data) {
  auto* self = static_cast<ManetteManager*>(user_data);
  if (!self->callback_) return;

  auto it = self->devices_.find(device);
  if (it == self->devices_.end()) return;

  uint16_t axis;
  int8_t hat_value;
  if (!manette_event_get_hat(event, &axis, &hat_value)) return;

  int64_t ts = NowMillis();
  const std::string& gid = it->second.id;

  if (axis == ABS_HAT0X) {
    // Left/Right: hat_value -1 = left, +1 = right, 0 = released.
    {
      FlValue* ev = fl_value_new_map();
      fl_value_set_string_take(ev, "type", fl_value_new_string("button"));
      fl_value_set_string_take(ev, "gamepadId",
                               fl_value_new_string(gid.c_str()));
      fl_value_set_string_take(ev, "timestamp", fl_value_new_int(ts));
      fl_value_set_string_take(ev, "button",
                               fl_value_new_int(ButtonMapping::kDpadLeft));
      bool pressed = hat_value < 0;
      fl_value_set_string_take(ev, "pressed",
                               fl_value_new_bool(pressed ? TRUE : FALSE));
      fl_value_set_string_take(ev, "value",
                               fl_value_new_float(pressed ? 1.0 : 0.0));
      self->callback_(ev);
      fl_value_unref(ev);
    }
    {
      FlValue* ev = fl_value_new_map();
      fl_value_set_string_take(ev, "type", fl_value_new_string("button"));
      fl_value_set_string_take(ev, "gamepadId",
                               fl_value_new_string(gid.c_str()));
      fl_value_set_string_take(ev, "timestamp", fl_value_new_int(ts));
      fl_value_set_string_take(ev, "button",
                               fl_value_new_int(ButtonMapping::kDpadRight));
      bool pressed = hat_value > 0;
      fl_value_set_string_take(ev, "pressed",
                               fl_value_new_bool(pressed ? TRUE : FALSE));
      fl_value_set_string_take(ev, "value",
                               fl_value_new_float(pressed ? 1.0 : 0.0));
      self->callback_(ev);
      fl_value_unref(ev);
    }
  } else if (axis == ABS_HAT0Y) {
    // Up/Down: hat_value -1 = up, +1 = down, 0 = released.
    {
      FlValue* ev = fl_value_new_map();
      fl_value_set_string_take(ev, "type", fl_value_new_string("button"));
      fl_value_set_string_take(ev, "gamepadId",
                               fl_value_new_string(gid.c_str()));
      fl_value_set_string_take(ev, "timestamp", fl_value_new_int(ts));
      fl_value_set_string_take(ev, "button",
                               fl_value_new_int(ButtonMapping::kDpadUp));
      bool pressed = hat_value < 0;
      fl_value_set_string_take(ev, "pressed",
                               fl_value_new_bool(pressed ? TRUE : FALSE));
      fl_value_set_string_take(ev, "value",
                               fl_value_new_float(pressed ? 1.0 : 0.0));
      self->callback_(ev);
      fl_value_unref(ev);
    }
    {
      FlValue* ev = fl_value_new_map();
      fl_value_set_string_take(ev, "type", fl_value_new_string("button"));
      fl_value_set_string_take(ev, "gamepadId",
                               fl_value_new_string(gid.c_str()));
      fl_value_set_string_take(ev, "timestamp", fl_value_new_int(ts));
      fl_value_set_string_take(ev, "button",
                               fl_value_new_int(ButtonMapping::kDpadDown));
      bool pressed = hat_value > 0;
      fl_value_set_string_take(ev, "pressed",
                               fl_value_new_bool(pressed ? TRUE : FALSE));
      fl_value_set_string_take(ev, "value",
                               fl_value_new_float(pressed ? 1.0 : 0.0));
      self->callback_(ev);
      fl_value_unref(ev);
    }
  }
}

#endif  // HAVE_MANETTE
