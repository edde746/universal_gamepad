import 'package:flutter_test/flutter_test.dart';
import 'package:universal_gamepad/universal_gamepad.dart';

void main() {
  group('GamepadButton', () {
    test('has 17 values', () {
      expect(GamepadButton.values.length, 17);
    });

    test('indices match W3C standard', () {
      expect(GamepadButton.a.index, 0);
      expect(GamepadButton.b.index, 1);
      expect(GamepadButton.x.index, 2);
      expect(GamepadButton.y.index, 3);
      expect(GamepadButton.leftShoulder.index, 4);
      expect(GamepadButton.rightShoulder.index, 5);
      expect(GamepadButton.leftTrigger.index, 6);
      expect(GamepadButton.rightTrigger.index, 7);
      expect(GamepadButton.back.index, 8);
      expect(GamepadButton.start.index, 9);
      expect(GamepadButton.leftStickButton.index, 10);
      expect(GamepadButton.rightStickButton.index, 11);
      expect(GamepadButton.dpadUp.index, 12);
      expect(GamepadButton.dpadDown.index, 13);
      expect(GamepadButton.dpadLeft.index, 14);
      expect(GamepadButton.dpadRight.index, 15);
      expect(GamepadButton.guide.index, 16);
    });

    test('fromIndex returns correct button', () {
      expect(GamepadButton.fromIndex(0), GamepadButton.a);
      expect(GamepadButton.fromIndex(16), GamepadButton.guide);
    });

    test('fromIndex returns null for out of range', () {
      expect(GamepadButton.fromIndex(-1), isNull);
      expect(GamepadButton.fromIndex(17), isNull);
      expect(GamepadButton.fromIndex(100), isNull);
    });
  });

  group('GamepadAxis', () {
    test('has 4 values', () {
      expect(GamepadAxis.values.length, 4);
    });

    test('indices match W3C standard', () {
      expect(GamepadAxis.leftStickX.index, 0);
      expect(GamepadAxis.leftStickY.index, 1);
      expect(GamepadAxis.rightStickX.index, 2);
      expect(GamepadAxis.rightStickY.index, 3);
    });

    test('fromIndex returns correct axis', () {
      expect(GamepadAxis.fromIndex(0), GamepadAxis.leftStickX);
      expect(GamepadAxis.fromIndex(3), GamepadAxis.rightStickY);
    });

    test('fromIndex returns null for out of range', () {
      expect(GamepadAxis.fromIndex(-1), isNull);
      expect(GamepadAxis.fromIndex(4), isNull);
    });
  });

  group('GamepadInfo', () {
    test('fromMap creates correct instance', () {
      final info = GamepadInfo.fromMap({
        'id': 'test_0',
        'name': 'Test Controller',
        'vendorId': 1118,
        'productId': 736,
      });
      expect(info.id, 'test_0');
      expect(info.name, 'Test Controller');
      expect(info.vendorId, 1118);
      expect(info.productId, 736);
    });

    test('fromMap with null optional fields', () {
      final info = GamepadInfo.fromMap({
        'id': 'test_0',
        'name': 'Test Controller',
      });
      expect(info.vendorId, isNull);
      expect(info.productId, isNull);
    });

    test('toMap round-trips correctly', () {
      final info = GamepadInfo(
        id: 'test_0',
        name: 'Test Controller',
        vendorId: 1118,
        productId: 736,
      );
      final map = info.toMap();
      final restored = GamepadInfo.fromMap(map);
      expect(restored, info);
    });

    test('equality', () {
      final a = GamepadInfo(id: 'x', name: 'Y', vendorId: 1, productId: 2);
      final b = GamepadInfo(id: 'x', name: 'Y', vendorId: 1, productId: 2);
      final c = GamepadInfo(id: 'z', name: 'Y', vendorId: 1, productId: 2);
      expect(a, b);
      expect(a, isNot(c));
      expect(a.hashCode, b.hashCode);
    });
  });

  group('GamepadEvent.fromMap', () {
    test('decodes connection event', () {
      final event = GamepadEvent.fromMap({
        'type': 'connection',
        'gamepadId': 'test_0',
        'timestamp': 1706832000,
        'connected': true,
        'name': 'Xbox Controller',
        'vendorId': 1118,
        'productId': 736,
      });

      expect(event, isA<GamepadConnectionEvent>());
      final conn = event as GamepadConnectionEvent;
      expect(conn.gamepadId, 'test_0');
      expect(conn.timestamp, 1706832000);
      expect(conn.connected, true);
      expect(conn.info.name, 'Xbox Controller');
      expect(conn.info.vendorId, 1118);
      expect(conn.info.productId, 736);
    });

    test('decodes disconnection event', () {
      final event = GamepadEvent.fromMap({
        'type': 'connection',
        'gamepadId': 'test_0',
        'timestamp': 1706832000,
        'connected': false,
        'name': 'Xbox Controller',
      });

      final conn = event as GamepadConnectionEvent;
      expect(conn.connected, false);
    });

    test('decodes button event', () {
      final event = GamepadEvent.fromMap({
        'type': 'button',
        'gamepadId': 'test_0',
        'timestamp': 1706832000,
        'button': 0,
        'pressed': true,
        'value': 1.0,
      });

      expect(event, isA<GamepadButtonEvent>());
      final btn = event as GamepadButtonEvent;
      expect(btn.button, GamepadButton.a);
      expect(btn.pressed, true);
      expect(btn.value, 1.0);
    });

    test('decodes button event with analog value', () {
      final event = GamepadEvent.fromMap({
        'type': 'button',
        'gamepadId': 'test_0',
        'timestamp': 1706832000,
        'button': 6,
        'pressed': true,
        'value': 0.75,
      });

      final btn = event as GamepadButtonEvent;
      expect(btn.button, GamepadButton.leftTrigger);
      expect(btn.value, 0.75);
    });

    test('decodes axis event', () {
      final event = GamepadEvent.fromMap({
        'type': 'axis',
        'gamepadId': 'test_0',
        'timestamp': 1706832000,
        'axis': 0,
        'value': -0.5,
      });

      expect(event, isA<GamepadAxisEvent>());
      final axis = event as GamepadAxisEvent;
      expect(axis.axis, GamepadAxis.leftStickX);
      expect(axis.value, -0.5);
    });

    test('decodes all axis indices', () {
      for (var i = 0; i < 4; i++) {
        final event = GamepadEvent.fromMap({
          'type': 'axis',
          'gamepadId': 'test_0',
          'timestamp': 1706832000,
          'axis': i,
          'value': 0.0,
        });
        final axis = event as GamepadAxisEvent;
        expect(axis.axis, GamepadAxis.values[i]);
      }
    });

    test('throws on unknown event type', () {
      expect(
        () => GamepadEvent.fromMap({
          'type': 'unknown',
          'gamepadId': 'test_0',
          'timestamp': 1706832000,
        }),
        throwsArgumentError,
      );
    });

    test('throws on unknown button index', () {
      expect(
        () => GamepadEvent.fromMap({
          'type': 'button',
          'gamepadId': 'test_0',
          'timestamp': 1706832000,
          'button': 99,
          'pressed': true,
          'value': 1.0,
        }),
        throwsArgumentError,
      );
    });

    test('throws on unknown axis index', () {
      expect(
        () => GamepadEvent.fromMap({
          'type': 'axis',
          'gamepadId': 'test_0',
          'timestamp': 1706832000,
          'axis': 99,
          'value': 0.0,
        }),
        throwsArgumentError,
      );
    });

    test('handles int value as double for button', () {
      final event = GamepadEvent.fromMap({
        'type': 'button',
        'gamepadId': 'test_0',
        'timestamp': 1706832000,
        'button': 0,
        'pressed': true,
        'value': 1,
      });
      final btn = event as GamepadButtonEvent;
      expect(btn.value, 1.0);
    });

    test('handles int value as double for axis', () {
      final event = GamepadEvent.fromMap({
        'type': 'axis',
        'gamepadId': 'test_0',
        'timestamp': 1706832000,
        'axis': 0,
        'value': 0,
      });
      final axis = event as GamepadAxisEvent;
      expect(axis.value, 0.0);
    });
  });
}
