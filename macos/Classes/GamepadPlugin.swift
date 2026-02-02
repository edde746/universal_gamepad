import FlutterMacOS

/// Entry point for the gamepad macOS plugin.
///
/// Registers a `MethodChannel` (`dev.universal_gamepad/methods`) and an
/// `EventChannel` (`dev.universal_gamepad/events`) with the Flutter engine.
/// The EventChannel streams gamepad connection, button, and axis events.
/// The MethodChannel supports `listGamepads` and `dispose`.
public class GamepadPlugin: NSObject, FlutterPlugin {
    private let streamHandler: GamepadStreamHandler

    init(streamHandler: GamepadStreamHandler) {
        self.streamHandler = streamHandler
        super.init()
    }

    // MARK: - FlutterPlugin registration

    public static func register(with registrar: FlutterPluginRegistrar) {
        let streamHandler = GamepadStreamHandler()

        // Method channel
        let methodChannel = FlutterMethodChannel(
            name: "dev.universal_gamepad/methods",
            binaryMessenger: registrar.messenger
        )

        // Event channel
        let eventChannel = FlutterEventChannel(
            name: "dev.universal_gamepad/events",
            binaryMessenger: registrar.messenger
        )
        eventChannel.setStreamHandler(streamHandler)

        let instance = GamepadPlugin(streamHandler: streamHandler)
        registrar.addMethodCallDelegate(instance, channel: methodChannel)
    }

    // MARK: - FlutterPlugin method handling

    public func handle(_ call: FlutterMethodCall,
                       result: @escaping FlutterResult) {
        switch call.method {
        case "listGamepads":
            result(streamHandler.listGamepads())
        case "dispose":
            streamHandler.dispose()
            result(nil)
        default:
            result(FlutterMethodNotImplemented)
        }
    }
}
