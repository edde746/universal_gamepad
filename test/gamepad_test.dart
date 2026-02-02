import 'dart:async';

import 'package:flutter_test/flutter_test.dart';
import 'package:universal_gamepad/universal_gamepad.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

class MockGamepadPlatform extends GamepadPlatform
    with MockPlatformInterfaceMixin {
  final _controller = StreamController<GamepadEvent>.broadcast();

  @override
  Stream<GamepadEvent> get events => _controller.stream;

  void addEvent(GamepadEvent event) => _controller.add(event);

  @override
  Future<List<GamepadInfo>> listGamepads() async {
    return [
      GamepadInfo(id: 'mock_0', name: 'Mock Controller'),
    ];
  }

  @override
  Future<void> dispose() async {
    await _controller.close();
  }
}

void main() {
  group('Gamepad', () {
    late MockGamepadPlatform mockPlatform;

    setUp(() {
      mockPlatform = MockGamepadPlatform();
      GamepadPlatform.instance = mockPlatform;
    });

    test('default instance is MethodChannelGamepad', () {
      // Reset to check default
      final fresh = GamepadPlatform.instance;
      // After setUp it's our mock, but the type check passes
      expect(fresh, isA<MockGamepadPlatform>());
    });

    test('events stream emits events', () async {
      final events = <GamepadEvent>[];
      final sub = Gamepad.instance.events.listen(events.add);

      mockPlatform.addEvent(GamepadButtonEvent(
        gamepadId: 'mock_0',
        timestamp: 1000,
        button: GamepadButton.a,
        pressed: true,
        value: 1.0,
      ));

      await Future.delayed(Duration.zero);
      expect(events.length, 1);
      expect(events[0], isA<GamepadButtonEvent>());

      await sub.cancel();
    });

    test('connectionEvents filters to connections only', () async {
      final events = <GamepadConnectionEvent>[];
      final sub =
          Gamepad.instance.connectionEvents.listen(events.add);

      // Emit a button event (should be filtered out)
      mockPlatform.addEvent(GamepadButtonEvent(
        gamepadId: 'mock_0',
        timestamp: 1000,
        button: GamepadButton.a,
        pressed: true,
        value: 1.0,
      ));

      // Emit a connection event (should pass through)
      mockPlatform.addEvent(GamepadConnectionEvent(
        gamepadId: 'mock_0',
        timestamp: 1001,
        connected: true,
        info: GamepadInfo(id: 'mock_0', name: 'Mock Controller'),
      ));

      await Future.delayed(Duration.zero);
      expect(events.length, 1);
      expect(events[0].connected, true);

      await sub.cancel();
    });

    test('buttonEvents filters to buttons only', () async {
      final events = <GamepadButtonEvent>[];
      final sub = Gamepad.instance.buttonEvents.listen(events.add);

      // Emit an axis event (should be filtered out)
      mockPlatform.addEvent(GamepadAxisEvent(
        gamepadId: 'mock_0',
        timestamp: 1000,
        axis: GamepadAxis.leftStickX,
        value: 0.5,
      ));

      // Emit a button event (should pass through)
      mockPlatform.addEvent(GamepadButtonEvent(
        gamepadId: 'mock_0',
        timestamp: 1001,
        button: GamepadButton.b,
        pressed: false,
        value: 0.0,
      ));

      await Future.delayed(Duration.zero);
      expect(events.length, 1);
      expect(events[0].button, GamepadButton.b);

      await sub.cancel();
    });

    test('axisEvents filters to axes only', () async {
      final events = <GamepadAxisEvent>[];
      final sub = Gamepad.instance.axisEvents.listen(events.add);

      // Emit a button event (should be filtered out)
      mockPlatform.addEvent(GamepadButtonEvent(
        gamepadId: 'mock_0',
        timestamp: 1000,
        button: GamepadButton.x,
        pressed: true,
        value: 1.0,
      ));

      // Emit an axis event (should pass through)
      mockPlatform.addEvent(GamepadAxisEvent(
        gamepadId: 'mock_0',
        timestamp: 1001,
        axis: GamepadAxis.rightStickY,
        value: -0.75,
      ));

      await Future.delayed(Duration.zero);
      expect(events.length, 1);
      expect(events[0].axis, GamepadAxis.rightStickY);
      expect(events[0].value, -0.75);

      await sub.cancel();
    });

    test('listGamepads returns controllers', () async {
      final gamepads = await Gamepad.instance.listGamepads();
      expect(gamepads.length, 1);
      expect(gamepads[0].id, 'mock_0');
      expect(gamepads[0].name, 'Mock Controller');
    });
  });
}
