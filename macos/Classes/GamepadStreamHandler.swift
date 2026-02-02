import FlutterMacOS

/// FlutterStreamHandler that bridges native gamepad events to the Dart EventChannel.
///
/// When Flutter begins listening (`onListen`), the stream handler starts
/// the `GCControllerManager` which observes controller connections and input.
/// When Flutter cancels (`onCancel`), the manager is stopped and resources
/// are released.
class GamepadStreamHandler: NSObject, FlutterStreamHandler {
    private var eventSink: FlutterEventSink?
    private var manager: GCControllerManager?

    // MARK: - FlutterStreamHandler

    func onListen(withArguments arguments: Any?,
                  eventSink events: @escaping FlutterEventSink) -> FlutterError? {
        self.eventSink = events
        let mgr = GCControllerManager { [weak self] event in
            DispatchQueue.main.async {
                self?.eventSink?(event)
            }
        }
        self.manager = mgr
        mgr.start()
        return nil
    }

    func onCancel(withArguments arguments: Any?) -> FlutterError? {
        manager?.stop()
        manager = nil
        eventSink = nil
        return nil
    }

    // MARK: - Internal API

    /// Returns the list of currently connected gamepads as serialisable dictionaries.
    func listGamepads() -> [[String: Any]] {
        return manager?.listGamepads() ?? []
    }

    /// Tears down the manager and clears the event sink.
    func dispose() {
        manager?.stop()
        manager = nil
        eventSink = nil
    }
}
