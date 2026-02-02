#ifndef FLUTTER_PLUGIN_GAMEPAD_STREAM_HANDLER_H_
#define FLUTTER_PLUGIN_GAMEPAD_STREAM_HANDLER_H_

#include <flutter/encodable_value.h>
#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>

#include <functional>
#include <memory>
#include <mutex>

namespace gamepad {

/// StreamHandler for the gamepad event channel.
///
/// Holds the EventSink provided by Flutter and exposes a thread-safe
/// method to send events from any thread. Events are dispatched to the
/// Flutter/UI thread by posting through the event sink.
class GamepadStreamHandler
    : public flutter::StreamHandler<flutter::EncodableValue> {
 public:
  GamepadStreamHandler();
  ~GamepadStreamHandler() override;

  /// Sends a gamepad event to the Dart side. Thread-safe.
  /// The event should be a flutter::EncodableMap.
  void SendEvent(const flutter::EncodableValue& event);

  /// Returns true if a Dart listener is currently attached.
  bool HasListener() const;

 protected:
  std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>>
  OnListenInternal(
      const flutter::EncodableValue* arguments,
      std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&& events)
      override;

  std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>>
  OnCancelInternal(const flutter::EncodableValue* arguments) override;

 private:
  friend class ForwardingStreamHandler;

  mutable std::mutex sink_mutex_;
  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> event_sink_;
};

/// A thin forwarding StreamHandler that delegates OnListen/OnCancel to a
/// shared GamepadStreamHandler. This is needed because Flutter's
/// EventChannel::SetStreamHandler takes a unique_ptr, but the XInputManager
/// also needs access to the same handler to send events.
class ForwardingStreamHandler
    : public flutter::StreamHandler<flutter::EncodableValue> {
 public:
  explicit ForwardingStreamHandler(
      std::shared_ptr<GamepadStreamHandler> delegate);
  ~ForwardingStreamHandler() override;

 protected:
  std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>>
  OnListenInternal(
      const flutter::EncodableValue* arguments,
      std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&& events)
      override;

  std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>>
  OnCancelInternal(const flutter::EncodableValue* arguments) override;

 private:
  std::shared_ptr<GamepadStreamHandler> delegate_;
};

}  // namespace gamepad

#endif  // FLUTTER_PLUGIN_GAMEPAD_STREAM_HANDLER_H_
