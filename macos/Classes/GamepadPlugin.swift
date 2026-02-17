import FlutterMacOS

/// Entry point for the gamepad macOS plugin.
///
/// Registers a `MethodChannel` (`dev.universal_gamepad/methods`) and an
/// `EventChannel` (`dev.universal_gamepad/events`) with the Flutter engine.
/// The EventChannel streams gamepad connection, button, and axis events.
/// The MethodChannel supports `listGamepads` and `dispose`.
public class GamepadPlugin: NSObject, FlutterPlugin {

    private let controllerManager: GCControllerManager
    private let streamHandler: GamepadStreamHandler

    init(controllerManager: GCControllerManager, streamHandler: GamepadStreamHandler) {
        self.controllerManager = controllerManager
        self.streamHandler = streamHandler
        super.init()
    }

    // MARK: - FlutterPlugin registration

    public static func register(with registrar: FlutterPluginRegistrar) {
        let streamHandler = GamepadStreamHandler()
        let controllerManager = GCControllerManager(streamHandler: streamHandler)

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

        let instance = GamepadPlugin(
            controllerManager: controllerManager,
            streamHandler: streamHandler
        )
        registrar.addMethodCallDelegate(instance, channel: methodChannel)

        // Start observing controllers immediately so we capture connections
        // that happen before the Dart side listens.
        controllerManager.start()
    }

    // MARK: - FlutterPlugin method handling

    public func handle(_ call: FlutterMethodCall,
                       result: @escaping FlutterResult) {
        switch call.method {
        case "listGamepads":
            result(controllerManager.listGamepads())
        case "dispose":
            controllerManager.stop()
            result(nil)
        default:
            result(FlutterMethodNotImplemented)
        }
    }
}
