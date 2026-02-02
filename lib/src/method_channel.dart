import 'dart:async';

import 'package:flutter/services.dart';

import 'platform_interface.dart';
import 'types/gamepad_event.dart';
import 'types/gamepad_info.dart';

/// Implementation of [GamepadPlatform] using EventChannel and MethodChannel.
class MethodChannelGamepad extends GamepadPlatform {
  final _methodChannel = const MethodChannel('dev.universal_gamepad/methods');
  final _eventChannel = const EventChannel('dev.universal_gamepad/events');

  Stream<GamepadEvent>? _events;

  @override
  Stream<GamepadEvent> get events {
    _events ??= _eventChannel.receiveBroadcastStream().map((dynamic event) {
      final map = Map<String, dynamic>.from(event as Map);
      return GamepadEvent.fromMap(map);
    });
    return _events!;
  }

  @override
  Future<List<GamepadInfo>> listGamepads() async {
    final result =
        await _methodChannel.invokeListMethod<Map>('listGamepads') ?? [];
    return result
        .map((m) => GamepadInfo.fromMap(Map<String, dynamic>.from(m)))
        .toList();
  }

  @override
  Future<void> dispose() async {
    await _methodChannel.invokeMethod<void>('dispose');
    _events = null;
  }
}
