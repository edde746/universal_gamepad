package dev.universal_gamepad

import android.content.Context
import android.hardware.input.InputManager
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import kotlin.math.abs

/**
 * Manages gamepad InputDevice tracking and translates Android input events into
 * compact positional lists sent to Dart via [GamepadStreamHandler].
 *
 * Uses [InputManager.InputDeviceListener] for connect/disconnect detection and
 * processes [MotionEvent] (axes) and [KeyEvent] (buttons) from the FlutterView
 * listeners.
 */
class GamepadInputManager(
    context: Context,
    private val streamHandler: GamepadStreamHandler,
) : InputManager.InputDeviceListener {

    private val inputManager: InputManager =
        context.getSystemService(Context.INPUT_SERVICE) as InputManager

    /**
     * Set of currently tracked gamepad device IDs.
     * Used to detect new connections and emit disconnect events.
     */
    private val trackedDevices = mutableSetOf<Int>()

    /**
     * Previous axis values keyed by "(deviceId)_(axisConstant)".
     * Used to filter out duplicate / jitter axis events.
     */
    private val previousAxisValues = mutableMapOf<String, Float>()

    /**
     * Previous D-pad hat state keyed by deviceId.
     * Stores the last emitted hat-X and hat-Y values so we can emit proper
     * press/release button events when the hat changes.
     */
    private val previousHatX = mutableMapOf<Int, Float>()
    private val previousHatY = mutableMapOf<Int, Float>()

    /** Threshold below which axis value changes are ignored. */
    private val AXIS_THRESHOLD = 0.01f

    // -- Lifecycle -------------------------------------------------------------

    /**
     * Begin listening for input device changes. Also performs an initial scan
     * of connected gamepads to track them.
     */
    fun start() {
        inputManager.registerInputDeviceListener(this, null)
        scanConnectedGamepads()
    }

    /**
     * Stop listening for input device changes and clear state.
     */
    fun stop() {
        inputManager.unregisterInputDeviceListener(this)
        trackedDevices.clear()
        previousAxisValues.clear()
        previousHatX.clear()
        previousHatY.clear()
    }

    // -- InputManager.InputDeviceListener --------------------------------------

    override fun onInputDeviceAdded(deviceId: Int) {
        val device = InputDevice.getDevice(deviceId) ?: return
        if (!isGamepad(device)) return

        if (trackedDevices.add(deviceId)) {
            emitConnectionEvent(device, connected = true)
        }
    }

    override fun onInputDeviceRemoved(deviceId: Int) {
        if (trackedDevices.remove(deviceId)) {
            emitDisconnectionEvent(deviceId)
            // Clean up axis state for removed device.
            previousAxisValues.keys
                .filter { it.startsWith("${deviceId}_") }
                .forEach { previousAxisValues.remove(it) }
            previousHatX.remove(deviceId)
            previousHatY.remove(deviceId)
        }
    }

    override fun onInputDeviceChanged(deviceId: Int) {
        // A device was reconfigured. Treat it like a reconnect if it is
        // (or has become) a gamepad we were not yet tracking.
        val device = InputDevice.getDevice(deviceId) ?: return
        if (isGamepad(device) && trackedDevices.add(deviceId)) {
            emitConnectionEvent(device, connected = true)
        }
    }

    // -- Public input handlers (called from plugin view listeners) --------------

    /**
     * Processes a generic motion event (joystick axes, triggers, D-pad hat).
     * Returns `true` if the event was handled.
     */
    fun onGenericMotionEvent(event: MotionEvent): Boolean {
        if (event.source and (InputDevice.SOURCE_GAMEPAD or InputDevice.SOURCE_JOYSTICK) == 0) {
            return false
        }

        val deviceId = event.deviceId
        val timestamp = currentTimestamp()
        var handled = false

        // Ensure device is tracked.
        ensureTracked(deviceId)

        // -- Stick axes --------------------------------------------------------
        for (axis in ButtonMapping.STICK_AXES) {
            val value = event.getAxisValue(axis)
            val key = "${deviceId}_${axis}"
            val prev = previousAxisValues[key] ?: 0f
            if (abs(value - prev) > AXIS_THRESHOLD) {
                previousAxisValues[key] = value
                val w3cAxis = ButtonMapping.axisIndexForMotionAxis(axis)
                if (w3cAxis != null) {
                    emitAxisEvent(deviceId, timestamp, w3cAxis, value.toDouble())
                    handled = true
                }
            }
        }

        // -- Trigger axes (emit as button events with analog value) ------------
        for (axis in ButtonMapping.TRIGGER_AXES) {
            val value = event.getAxisValue(axis)
            val key = "${deviceId}_${axis}"
            val prev = previousAxisValues[key] ?: 0f
            if (abs(value - prev) > AXIS_THRESHOLD) {
                previousAxisValues[key] = value
                val buttonIndex = ButtonMapping.triggerButtonIndexForAxis(axis)
                if (buttonIndex != null) {
                    val pressed = value > 0.1f
                    emitButtonEvent(deviceId, timestamp, buttonIndex, pressed, value.toDouble())
                    handled = true
                }
            }
        }

        // -- Hat axes (D-pad via analog hat, convert to button events) ---------
        handled = processHatAxes(event, deviceId, timestamp) || handled

        return handled
    }

    /**
     * Processes a key event from a gamepad button.
     * Returns `true` if the event was handled.
     */
    fun onKeyEvent(event: KeyEvent): Boolean {
        if (event.source and (InputDevice.SOURCE_GAMEPAD or InputDevice.SOURCE_JOYSTICK) == 0) {
            return false
        }

        val buttonIndex = ButtonMapping.buttonIndexForKeyCode(event.keyCode) ?: return false
        val deviceId = event.deviceId
        val timestamp = currentTimestamp()

        // Ensure device is tracked.
        ensureTracked(deviceId)

        val pressed = event.action == KeyEvent.ACTION_DOWN
        val value = if (pressed) 1.0 else 0.0

        // Only emit on ACTION_DOWN and ACTION_UP; ignore repeats for press.
        if (event.action == KeyEvent.ACTION_DOWN || event.action == KeyEvent.ACTION_UP) {
            emitButtonEvent(deviceId, timestamp, buttonIndex, pressed, value)
            return true
        }

        return false
    }

    // -- List gamepads ---------------------------------------------------------

    /**
     * Returns a list of currently connected gamepad info maps suitable for
     * sending over the method channel.
     */
    fun listGamepads(): List<HashMap<String, Any>> {
        val result = mutableListOf<HashMap<String, Any>>()
        for (id in InputDevice.getDeviceIds()) {
            val device = InputDevice.getDevice(id) ?: continue
            if (!isGamepad(device)) continue
            result.add(gamepadInfoMap(device))
        }
        return result
    }

    // -- Private helpers -------------------------------------------------------

    /** Scans for currently connected gamepads, tracks them and emits connection events. */
    private fun scanConnectedGamepads() {
        for (id in InputDevice.getDeviceIds()) {
            val device = InputDevice.getDevice(id) ?: continue
            if (isGamepad(device)) {
                if (trackedDevices.add(id)) {
                    emitConnectionEvent(device, connected = true)
                }
            }
        }
    }

    /** Ensures a device is in the tracked set, emitting a connection event if new. */
    private fun ensureTracked(deviceId: Int) {
        if (!trackedDevices.contains(deviceId)) {
            val device = InputDevice.getDevice(deviceId) ?: return
            if (isGamepad(device) && trackedDevices.add(deviceId)) {
                emitConnectionEvent(device, connected = true)
            }
        }
    }

    /** Returns whether the device is a real gamepad or joystick. */
    private fun isGamepad(device: InputDevice): Boolean {
        val sources = device.sources
        val hasGamepadSource = (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
        val hasJoystickSource = (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
        if (!hasGamepadSource && !hasJoystickSource) return false

        // Exclude touchscreen digitizers and mice that report joystick source.
        if ((sources and InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN) return false
        if ((sources and InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE) return false

        // Require at least one standard gamepad button to be present.
        // Filters out virtual devices (e.g. "uinput-goodix") that report
        // SOURCE_JOYSTICK but have no actual gamepad controls.
        val hasGamepadButtons = device.hasKeys(
            KeyEvent.KEYCODE_BUTTON_A,
            KeyEvent.KEYCODE_BUTTON_B,
            KeyEvent.KEYCODE_BUTTON_X,
            KeyEvent.KEYCODE_BUTTON_Y,
            KeyEvent.KEYCODE_BUTTON_START,
        ).any { it }
        if (!hasGamepadButtons) return false

        return true
    }

    /** Returns the current timestamp as milliseconds since the Unix epoch. */
    private fun currentTimestamp(): Long = System.currentTimeMillis()

    /**
     * Processes AXIS_HAT_X and AXIS_HAT_Y from a MotionEvent, converting
     * discrete hat positions into D-pad button press/release events.
     */
    private fun processHatAxes(
        event: MotionEvent,
        deviceId: Int,
        timestamp: Long,
    ): Boolean {
        var handled = false

        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)

        val prevHatX = previousHatX[deviceId] ?: 0f
        val prevHatY = previousHatY[deviceId] ?: 0f

        if (hatX != prevHatX) {
            previousHatX[deviceId] = hatX

            // Release old direction if it was pressed.
            if (prevHatX < -0.5f) {
                emitButtonEvent(deviceId, timestamp, ButtonMapping.DPAD_LEFT, pressed = false, value = 0.0)
                handled = true
            } else if (prevHatX > 0.5f) {
                emitButtonEvent(deviceId, timestamp, ButtonMapping.DPAD_RIGHT, pressed = false, value = 0.0)
                handled = true
            }

            // Press new direction.
            if (hatX < -0.5f) {
                emitButtonEvent(deviceId, timestamp, ButtonMapping.DPAD_LEFT, pressed = true, value = 1.0)
                handled = true
            } else if (hatX > 0.5f) {
                emitButtonEvent(deviceId, timestamp, ButtonMapping.DPAD_RIGHT, pressed = true, value = 1.0)
                handled = true
            }
        }

        if (hatY != prevHatY) {
            previousHatY[deviceId] = hatY

            // Release old direction if it was pressed.
            if (prevHatY < -0.5f) {
                emitButtonEvent(deviceId, timestamp, ButtonMapping.DPAD_UP, pressed = false, value = 0.0)
                handled = true
            } else if (prevHatY > 0.5f) {
                emitButtonEvent(deviceId, timestamp, ButtonMapping.DPAD_DOWN, pressed = false, value = 0.0)
                handled = true
            }

            // Press new direction.
            if (hatY < -0.5f) {
                emitButtonEvent(deviceId, timestamp, ButtonMapping.DPAD_UP, pressed = true, value = 1.0)
                handled = true
            } else if (hatY > 0.5f) {
                emitButtonEvent(deviceId, timestamp, ButtonMapping.DPAD_DOWN, pressed = true, value = 1.0)
                handled = true
            }
        }

        return handled
    }

    // -- Event emission --------------------------------------------------------

    private fun emitButtonEvent(
        gamepadId: Int,
        timestamp: Long,
        button: Int,
        pressed: Boolean,
        value: Double,
    ) {
        // Wire format: [1, gamepadId, timestamp, buttonIndex, pressed, value]
        streamHandler.send(listOf(1, gamepadId, timestamp, button, pressed, value))
    }

    private fun emitAxisEvent(
        gamepadId: Int,
        timestamp: Long,
        axis: Int,
        value: Double,
    ) {
        // Wire format: [2, gamepadId, timestamp, axisIndex, value]
        streamHandler.send(listOf(2, gamepadId, timestamp, axis, value))
    }

    private fun emitConnectionEvent(device: InputDevice, connected: Boolean) {
        val timestamp = currentTimestamp()
        // Wire format: [0, gamepadId, timestamp, connected, name, vendorId, productId]
        streamHandler.send(listOf(
            0,
            device.id,
            timestamp,
            connected,
            device.name ?: "Unknown",
            device.vendorId,
            device.productId,
        ))
    }

    private fun emitDisconnectionEvent(deviceId: Int) {
        val timestamp = currentTimestamp()
        // Wire format: [0, gamepadId, timestamp, connected, name, vendorId, productId]
        streamHandler.send(listOf(0, deviceId, timestamp, false, "Unknown", 0, 0))
    }

    private fun gamepadInfoMap(device: InputDevice): HashMap<String, Any> {
        return hashMapOf(
            "id" to device.id,
            "name" to (device.name ?: "Unknown"),
            "vendorId" to device.vendorId,
            "productId" to device.productId,
        )
    }
}
