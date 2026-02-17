#include "gamepad_plugin.h"

#include "gamepad_stream_handler.h"
#include "manette_manager.h"

#include <cstring>
#include <memory>

// Plugin state, allocated once per registration and freed on teardown.
struct GamepadPlugin {
  std::unique_ptr<GamepadStreamHandler> stream_handler;
  std::unique_ptr<ManetteManager> manager;
  FlMethodChannel* method_channel;
  FlEventChannel* event_channel;
};

static GamepadPlugin* g_plugin = nullptr;

// ---------------------------------------------------------------------------
// MethodChannel handler
// ---------------------------------------------------------------------------

static void method_call_cb(FlMethodChannel* channel,
                           FlMethodCall* method_call,
                           gpointer user_data) {
  auto* plugin = static_cast<GamepadPlugin*>(user_data);
  const gchar* method = fl_method_call_get_name(method_call);

  if (strcmp(method, "listGamepads") == 0) {
    FlValue* result = plugin->manager->ListGamepads();
    g_autoptr(FlMethodResponse) response =
        FL_METHOD_RESPONSE(fl_method_success_response_new(result));
    fl_value_unref(result);
    g_autoptr(GError) error = nullptr;
    fl_method_call_respond(method_call, response, &error);
    if (error) {
      g_warning("gamepad: failed to respond to listGamepads: %s",
                error->message);
    }
  } else if (strcmp(method, "dispose") == 0) {
    plugin->manager->Stop();
    g_autoptr(FlMethodResponse) response =
        FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    g_autoptr(GError) error = nullptr;
    fl_method_call_respond(method_call, response, &error);
    if (error) {
      g_warning("gamepad: failed to respond to dispose: %s",
                error->message);
    }
  } else {
    g_autoptr(FlMethodResponse) response =
        FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
    g_autoptr(GError) error = nullptr;
    fl_method_call_respond(method_call, response, &error);
    if (error) {
      g_warning("gamepad: failed to respond with not-implemented: %s",
                error->message);
    }
  }
}

// ---------------------------------------------------------------------------
// Plugin registration (C entry point)
// ---------------------------------------------------------------------------

void gamepad_plugin_register_with_registrar(
    FlPluginRegistrar* registrar) {
  // Clean up any previous registration (defensive).
  if (g_plugin) {
    delete g_plugin;
    g_plugin = nullptr;
  }

  g_plugin = new GamepadPlugin();
  g_plugin->stream_handler = std::make_unique<GamepadStreamHandler>();
  g_plugin->manager = std::make_unique<ManetteManager>();

  FlBinaryMessenger* messenger =
      fl_plugin_registrar_get_messenger(registrar);

  // Set up the MethodChannel.
  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();

  g_plugin->method_channel = fl_method_channel_new(
      messenger, "dev.universal_gamepad/methods", FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(
      g_plugin->method_channel, method_call_cb, g_plugin, nullptr);

  // Set up the EventChannel.
  g_plugin->event_channel = fl_event_channel_new(
      messenger, "dev.universal_gamepad/events", FL_METHOD_CODEC(codec));
  g_plugin->stream_handler->SetChannel(g_plugin->event_channel);

  // Wire the stream handler to the manager: when Dart starts listening
  // we start monitoring, and when Dart cancels we stop.
  GamepadStreamHandler* handler = g_plugin->stream_handler.get();
  ManetteManager* manager = g_plugin->manager.get();

  g_plugin->stream_handler->SetListenCallback(
      [manager, handler](bool listening) {
        if (listening) {
          manager->Start([handler](FlValue* event) {
            handler->SendEvent(event);
          });
        } else {
          manager->Stop();
        }
      });
}
