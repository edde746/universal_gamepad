#include "gamepad_stream_handler.h"

namespace gamepad {

// ---------------------------------------------------------------------------
// GamepadStreamHandler
// ---------------------------------------------------------------------------

GamepadStreamHandler::GamepadStreamHandler() = default;
GamepadStreamHandler::~GamepadStreamHandler() = default;

void GamepadStreamHandler::SendEvent(const flutter::EncodableValue& event) {
  std::lock_guard<std::mutex> lock(sink_mutex_);
  if (event_sink_) {
    event_sink_->Success(event);
  }
}

bool GamepadStreamHandler::HasListener() const {
  std::lock_guard<std::mutex> lock(sink_mutex_);
  return event_sink_ != nullptr;
}

std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>>
GamepadStreamHandler::OnListenInternal(
    const flutter::EncodableValue* arguments,
    std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&& events) {
  std::lock_guard<std::mutex> lock(sink_mutex_);
  event_sink_ = std::move(events);
  return nullptr;
}

std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>>
GamepadStreamHandler::OnCancelInternal(
    const flutter::EncodableValue* arguments) {
  std::lock_guard<std::mutex> lock(sink_mutex_);
  event_sink_ = nullptr;
  return nullptr;
}

// ---------------------------------------------------------------------------
// ForwardingStreamHandler
// ---------------------------------------------------------------------------

ForwardingStreamHandler::ForwardingStreamHandler(
    std::shared_ptr<GamepadStreamHandler> delegate)
    : delegate_(std::move(delegate)) {}

ForwardingStreamHandler::~ForwardingStreamHandler() = default;

std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>>
ForwardingStreamHandler::OnListenInternal(
    const flutter::EncodableValue* arguments,
    std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&& events) {
  // Forward the sink to the shared handler.
  return delegate_->OnListenInternal(arguments, std::move(events));
}

std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>>
ForwardingStreamHandler::OnCancelInternal(
    const flutter::EncodableValue* arguments) {
  return delegate_->OnCancelInternal(arguments);
}

}  // namespace gamepad
