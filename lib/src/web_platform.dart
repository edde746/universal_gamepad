import 'dart:async';
import 'dart:js_interop';

import 'package:flutter_web_plugins/flutter_web_plugins.dart';
import 'package:web/web.dart' as web;

import 'platform_interface.dart';
import 'types/gamepad_event.dart';
import 'types/gamepad_info.dart';

/// Web implementation of [GamepadPlatform] using the W3C Gamepad API.
class WebGamepad extends GamepadPlatform {
  WebGamepad._();

  static void registerWith(Registrar registrar) {
    GamepadPlatform.instance = WebGamepad._();
  }

  StreamController<GamepadEvent>? _controller;
  int? _animationFrameId;
  final Map<int, _GamepadState> _previousStates = {};

  web.EventHandler? _connectHandler;
  web.EventHandler? _disconnectHandler;

  @override
  Stream<GamepadEvent> get events {
    if (_controller == null) {
      _controller = StreamController<GamepadEvent>.broadcast(
        onListen: _startPolling,
        onCancel: _stopPolling,
      );
    }
    return _controller!.stream;
  }

  @override
  Future<List<GamepadInfo>> listGamepads() async {
    final gamepads = web.window.navigator.getGamepads().toDart;
    final result = <GamepadInfo>[];
    for (final gp in gamepads) {
      if (gp == null) continue;
      final gamepad = gp as web.Gamepad;
      result.add(GamepadInfo(
        id: 'web_${gamepad.index}',
        name: gamepad.id,
      ));
    }
    return result;
  }

  @override
  Future<void> dispose() async {
    _stopPolling();
    await _controller?.close();
    _controller = null;
  }

  void _startPolling() {
    _connectHandler = (web.Event event) {
      final ge = event as web.GamepadEvent;
      final gamepad = ge.gamepad;
      final now = DateTime.now().millisecondsSinceEpoch;
      _controller?.add(GamepadConnectionEvent(
        gamepadId: 'web_${gamepad.index}',
        timestamp: now,
        connected: true,
        info: GamepadInfo(
          id: 'web_${gamepad.index}',
          name: gamepad.id,
        ),
      ));
    }.toJS;

    _disconnectHandler = (web.Event event) {
      final ge = event as web.GamepadEvent;
      final gamepad = ge.gamepad;
      final now = DateTime.now().millisecondsSinceEpoch;
      _previousStates.remove(gamepad.index);
      _controller?.add(GamepadConnectionEvent(
        gamepadId: 'web_${gamepad.index}',
        timestamp: now,
        connected: false,
        info: GamepadInfo(
          id: 'web_${gamepad.index}',
          name: gamepad.id,
        ),
      ));
    }.toJS;

    web.window.addEventListener('gamepadconnected', _connectHandler);
    web.window.addEventListener('gamepaddisconnected', _disconnectHandler);

    _pollFrame(0.0.toJS);
  }

  void _stopPolling() {
    if (_animationFrameId != null) {
      web.window.cancelAnimationFrame(_animationFrameId!);
      _animationFrameId = null;
    }
    if (_connectHandler != null) {
      web.window.removeEventListener('gamepadconnected', _connectHandler);
      _connectHandler = null;
    }
    if (_disconnectHandler != null) {
      web.window.removeEventListener('gamepaddisconnected', _disconnectHandler);
      _disconnectHandler = null;
    }
    _previousStates.clear();
  }

  void _pollFrame(JSNumber timestamp) {
    _diffGamepadState();
    _animationFrameId = web.window.requestAnimationFrame(_pollFrame.toJS);
  }

  void _diffGamepadState() {
    final gamepads = web.window.navigator.getGamepads().toDart;
    final now = DateTime.now().millisecondsSinceEpoch;

    for (final gp in gamepads) {
      if (gp == null) continue;
      final gamepad = gp as web.Gamepad;
      final id = gamepad.index;
      final gamepadId = 'web_$id';

      final buttons = gamepad.buttons.toDart;
      final axes = gamepad.axes.toDart;

      final prev = _previousStates[id];

      // Check buttons
      for (var i = 0; i < buttons.length && i < 17; i++) {
        final button = buttons[i] as web.GamepadButton;
        final pressed = button.pressed;
        final value = button.value;
        final prevPressed = prev?.buttonPressed[i] ?? false;
        final prevValue = prev?.buttonValues[i] ?? 0.0;

        if (pressed != prevPressed || (value - prevValue).abs() > 0.01) {
          _controller?.add(GamepadButtonEvent.fromMap({
            'type': 'button',
            'gamepadId': gamepadId,
            'timestamp': now,
            'button': i,
            'pressed': pressed,
            'value': value,
          }));
        }
      }

      // Check axes
      for (var i = 0; i < axes.length && i < 4; i++) {
        final value = (axes[i] as JSNumber).toDartDouble;
        final prevValue = prev?.axisValues[i] ?? 0.0;

        if ((value - prevValue).abs() > 0.01) {
          _controller?.add(GamepadAxisEvent.fromMap({
            'type': 'axis',
            'gamepadId': gamepadId,
            'timestamp': now,
            'axis': i,
            'value': value,
          }));
        }
      }

      // Update stored state
      _previousStates[id] = _GamepadState(
        buttonPressed: [
          for (var i = 0; i < buttons.length && i < 17; i++)
            (buttons[i] as web.GamepadButton).pressed,
        ],
        buttonValues: [
          for (var i = 0; i < buttons.length && i < 17; i++)
            (buttons[i] as web.GamepadButton).value,
        ],
        axisValues: [
          for (var i = 0; i < axes.length && i < 4; i++)
            (axes[i] as JSNumber).toDartDouble,
        ],
      );
    }
  }
}

class _GamepadState {
  _GamepadState({
    required this.buttonPressed,
    required this.buttonValues,
    required this.axisValues,
  });

  final List<bool> buttonPressed;
  final List<double> buttonValues;
  final List<double> axisValues;
}
