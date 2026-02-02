import 'gamepad_axis.dart';
import 'gamepad_button.dart';
import 'gamepad_info.dart';

/// Base class for all gamepad events.
sealed class GamepadEvent {
  const GamepadEvent({
    required this.gamepadId,
    required this.timestamp,
  });

  /// Identifier of the gamepad that produced this event.
  final String gamepadId;

  /// Timestamp of the event in milliseconds since epoch.
  final int timestamp;

  /// Deserializes a [GamepadEvent] from a platform channel map.
  factory GamepadEvent.fromMap(Map<String, dynamic> map) {
    final type = map['type'] as String;
    return switch (type) {
      'connection' => GamepadConnectionEvent.fromMap(map),
      'button' => GamepadButtonEvent.fromMap(map),
      'axis' => GamepadAxisEvent.fromMap(map),
      _ => throw ArgumentError('Unknown gamepad event type: $type'),
    };
  }
}

/// Fired when a gamepad is connected or disconnected.
class GamepadConnectionEvent extends GamepadEvent {
  const GamepadConnectionEvent({
    required super.gamepadId,
    required super.timestamp,
    required this.connected,
    required this.info,
  });

  /// Whether the gamepad was connected (`true`) or disconnected (`false`).
  final bool connected;

  /// Information about the gamepad.
  final GamepadInfo info;

  factory GamepadConnectionEvent.fromMap(Map<String, dynamic> map) {
    return GamepadConnectionEvent(
      gamepadId: map['gamepadId'] as String,
      timestamp: map['timestamp'] as int,
      connected: map['connected'] as bool,
      info: GamepadInfo(
        id: map['gamepadId'] as String,
        name: (map['name'] as String?) ?? 'Unknown',
        vendorId: map['vendorId'] as int?,
        productId: map['productId'] as int?,
      ),
    );
  }
}

/// Fired when a gamepad button is pressed or released.
class GamepadButtonEvent extends GamepadEvent {
  const GamepadButtonEvent({
    required super.gamepadId,
    required super.timestamp,
    required this.button,
    required this.pressed,
    required this.value,
  });

  /// The button that changed state.
  final GamepadButton button;

  /// Whether the button is currently pressed.
  final bool pressed;

  /// Analog value of the button press (0.0 = released, 1.0 = fully pressed).
  /// For digital buttons, this will be 0.0 or 1.0.
  final double value;

  factory GamepadButtonEvent.fromMap(Map<String, dynamic> map) {
    final buttonIndex = map['button'] as int;
    final button = GamepadButton.fromIndex(buttonIndex);
    if (button == null) {
      throw ArgumentError('Unknown button index: $buttonIndex');
    }
    return GamepadButtonEvent(
      gamepadId: map['gamepadId'] as String,
      timestamp: map['timestamp'] as int,
      button: button,
      pressed: map['pressed'] as bool,
      value: (map['value'] as num).toDouble(),
    );
  }
}

/// Fired when a gamepad axis value changes.
class GamepadAxisEvent extends GamepadEvent {
  const GamepadAxisEvent({
    required super.gamepadId,
    required super.timestamp,
    required this.axis,
    required this.value,
  });

  /// The axis that changed value.
  final GamepadAxis axis;

  /// Current axis value (-1.0 to 1.0).
  final double value;

  factory GamepadAxisEvent.fromMap(Map<String, dynamic> map) {
    final axisIndex = map['axis'] as int;
    final axis = GamepadAxis.fromIndex(axisIndex);
    if (axis == null) {
      throw ArgumentError('Unknown axis index: $axisIndex');
    }
    return GamepadAxisEvent(
      gamepadId: map['gamepadId'] as String,
      timestamp: map['timestamp'] as int,
      axis: axis,
      value: (map['value'] as num).toDouble(),
    );
  }
}
