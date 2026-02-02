package dev.gamepad

import android.app.Activity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.activity.ActivityAware
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

/**
 * Android platform implementation of the gamepad Flutter plugin.
 *
 * Registers a [MethodChannel] (`dev.gamepad/methods`) and an
 * [EventChannel] (`dev.gamepad/events`) with the Flutter engine.
 *
 * Attaches [View.OnGenericMotionListener] and [View.OnKeyListener] on the
 * FlutterView to intercept gamepad input and forward it to Dart through
 * the [GamepadInputManager].
 */
class GamepadPlugin : FlutterPlugin, ActivityAware, MethodChannel.MethodCallHandler {

    private var methodChannel: MethodChannel? = null
    private var eventChannel: EventChannel? = null

    private var streamHandler: GamepadStreamHandler? = null
    private var inputManager: GamepadInputManager? = null

    private var flutterPluginBinding: FlutterPlugin.FlutterPluginBinding? = null
    private var activityBinding: ActivityPluginBinding? = null

    /** The view we've attached our input listeners to. */
    private var attachedView: View? = null

    // -- FlutterPlugin ---------------------------------------------------------

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        flutterPluginBinding = binding

        val handler = GamepadStreamHandler()
        streamHandler = handler

        methodChannel = MethodChannel(binding.binaryMessenger, "dev.gamepad/methods").also {
            it.setMethodCallHandler(this)
        }

        eventChannel = EventChannel(binding.binaryMessenger, "dev.gamepad/events").also {
            it.setStreamHandler(handler)
        }

        val manager = GamepadInputManager(binding.applicationContext, handler)
        inputManager = manager
        manager.start()
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        tearDown()
        flutterPluginBinding = null
    }

    // -- MethodChannel.MethodCallHandler ---------------------------------------

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "listGamepads" -> {
                result.success(inputManager?.listGamepads() ?: emptyList<HashMap<String, Any>>())
            }
            "dispose" -> {
                inputManager?.stop()
                result.success(null)
            }
            else -> result.notImplemented()
        }
    }

    // -- ActivityAware ---------------------------------------------------------

    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        activityBinding = binding
        attachInputListeners(binding.activity)
    }

    override fun onDetachedFromActivityForConfigChanges() {
        detachInputListeners()
        activityBinding = null
    }

    override fun onReattachedToActivityForConfigChanges(binding: ActivityPluginBinding) {
        activityBinding = binding
        attachInputListeners(binding.activity)
    }

    override fun onDetachedFromActivity() {
        detachInputListeners()
        activityBinding = null
    }

    // -- Input listener management ---------------------------------------------

    /**
     * Attaches [View.OnGenericMotionListener] and [View.OnKeyListener] to the
     * root content view of the activity so that gamepad events are intercepted
     * before they reach Flutter's default handling.
     */
    private fun attachInputListeners(activity: Activity) {
        // Use the decor view which receives all input events.
        val view = activity.window?.decorView ?: return
        attachedView = view

        view.setOnGenericMotionListener { _, event ->
            inputManager?.onGenericMotionEvent(event) ?: false
        }

        view.setOnKeyListener { _, _, event ->
            inputManager?.onKeyEvent(event) ?: false
        }
    }

    /**
     * Removes input listeners from the previously attached view.
     */
    private fun detachInputListeners() {
        attachedView?.setOnGenericMotionListener(null)
        attachedView?.setOnKeyListener(null)
        attachedView = null
    }

    /**
     * Full tear-down: stops the input manager, removes listeners, clears channels.
     */
    private fun tearDown() {
        inputManager?.stop()
        inputManager = null

        detachInputListeners()

        methodChannel?.setMethodCallHandler(null)
        methodChannel = null

        eventChannel?.setStreamHandler(null)
        eventChannel = null

        streamHandler = null
    }
}
