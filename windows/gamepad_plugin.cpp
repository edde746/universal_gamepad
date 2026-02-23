#include "gamepad_plugin.h"

// This must be included before many other Windows headers.
#include <windows.h>

#include <flutter/encodable_value.h>
#include <flutter/event_channel.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <memory>
#include <string>
#include <atomic>
#include <optional>

namespace gamepad {

// static
void GamepadPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  // Create the stream handler (shared between plugin and SDL manager).
  auto stream_handler = std::make_shared<GamepadStreamHandler>();

  // Create the SDL manager.
  auto sdl_manager = std::make_unique<SdlManager>(stream_handler);

  // Set up the MethodChannel.
  auto method_channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "dev.universal_gamepad/methods",
          &flutter::StandardMethodCodec::GetInstance());

  // Set up the EventChannel.
  auto event_channel =
      std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
          registrar->messenger(), "dev.universal_gamepad/events",
          &flutter::StandardMethodCodec::GetInstance());

  auto window_handle = std::make_shared<std::atomic<HWND>>(nullptr);
  const int window_proc_delegate_id =
      registrar->RegisterTopLevelWindowProcDelegate(
          [stream_handler, window_handle](HWND hwnd, UINT message, WPARAM wparam,
                                          LPARAM lparam) {
            window_handle->store(hwnd);
            if (message == GamepadStreamHandler::kFlushMessage) {
              stream_handler->FlushQueuedEvents();
              return std::optional<LRESULT>(0);
            }
            return std::optional<LRESULT>();
          });

  stream_handler->SetWakeCallback([window_handle]() {
    const HWND hwnd = window_handle->load();
    if (hwnd != nullptr) {
      ::PostMessage(hwnd, GamepadStreamHandler::kFlushMessage, 0, 0);
    }
  });

  // Create the plugin instance.
  auto plugin = std::make_unique<GamepadPlugin>(std::move(stream_handler),
                                                std::move(sdl_manager),
                                                registrar,
                                                window_proc_delegate_id);

  // Register the method call handler.
  method_channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto& call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  // Register the stream handler. SetStreamHandler requires a unique_ptr, so
  // we use a ForwardingStreamHandler that delegates to the shared handler
  // that the SdlManager also sends events through.
  event_channel->SetStreamHandler(
      std::make_unique<ForwardingStreamHandler>(plugin->stream_handler_));

  // Start polling immediately so we capture connections that happen before
  // the Dart side starts listening.
  plugin->sdl_manager_->StartPolling();

  // Transfer ownership to the registrar.
  registrar->AddPlugin(std::move(plugin));
}

GamepadPlugin::GamepadPlugin(
    std::shared_ptr<GamepadStreamHandler> stream_handler,
    std::unique_ptr<SdlManager> sdl_manager,
    flutter::PluginRegistrarWindows* registrar,
    int window_proc_delegate_id)
    : stream_handler_(std::move(stream_handler)),
      sdl_manager_(std::move(sdl_manager)),
      registrar_(registrar),
      window_proc_delegate_id_(window_proc_delegate_id) {}

GamepadPlugin::~GamepadPlugin() {
  if (sdl_manager_) {
    sdl_manager_->StopPolling();
  }
  if (registrar_) {
    registrar_->UnregisterTopLevelWindowProcDelegate(window_proc_delegate_id_);
  }
}

void GamepadPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  const std::string& method = method_call.method_name();

  if (method == "listGamepads") {
    auto gamepads = sdl_manager_->ListGamepads();
    result->Success(flutter::EncodableValue(gamepads));
  } else if (method == "dispose") {
    sdl_manager_->StopPolling();
    result->Success();
  } else {
    result->NotImplemented();
  }
}

}  // namespace gamepad

// ---------------------------------------------------------------------------
// C API wrapper - the entry point that the Flutter Windows embedder calls.
// ---------------------------------------------------------------------------

extern "C" {

__declspec(dllexport) void GamepadPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  gamepad::GamepadPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}

}  // extern "C"
