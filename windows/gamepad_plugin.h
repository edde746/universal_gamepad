#ifndef FLUTTER_PLUGIN_GAMEPAD_PLUGIN_H_
#define FLUTTER_PLUGIN_GAMEPAD_PLUGIN_H_

#include <flutter/event_channel.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>

#include <memory>

#include "gamepad_stream_handler.h"
#include "xinput_manager.h"

namespace gamepad {

/// Windows implementation of the gamepad Flutter plugin.
///
/// Registers an EventChannel (`dev.gamepad/events`) and a
/// MethodChannel (`dev.gamepad/methods`) with the Flutter engine.
/// Uses XInput to poll up to 4 gamepads on a background thread.
class GamepadPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);

  GamepadPlugin(
      std::shared_ptr<GamepadStreamHandler> stream_handler,
      std::unique_ptr<XInputManager> xinput_manager);

  ~GamepadPlugin() override;

  // Disallow copy and assign.
  GamepadPlugin(const GamepadPlugin&) = delete;
  GamepadPlugin& operator=(const GamepadPlugin&) = delete;

 private:
  /// Handles MethodChannel calls from Dart.
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  std::shared_ptr<GamepadStreamHandler> stream_handler_;
  std::unique_ptr<XInputManager> xinput_manager_;
};

}  // namespace gamepad

#endif  // FLUTTER_PLUGIN_GAMEPAD_PLUGIN_H_
