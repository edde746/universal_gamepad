import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:gamepad/gamepad.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  final methodChannel = const MethodChannel('dev.gamepad/methods');

  group('MethodChannelGamepad', () {
    late MethodChannelGamepad platform;

    setUp(() {
      platform = MethodChannelGamepad();
    });

    test('listGamepads decodes response', () async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(methodChannel, (call) async {
        if (call.method == 'listGamepads') {
          return [
            {
              'id': 'test_0',
              'name': 'Xbox Controller',
              'vendorId': 1118,
              'productId': 736,
            },
            {
              'id': 'test_1',
              'name': 'DualSense',
              'vendorId': 1356,
              'productId': 3302,
            },
          ];
        }
        return null;
      });

      final gamepads = await platform.listGamepads();
      expect(gamepads.length, 2);
      expect(gamepads[0].id, 'test_0');
      expect(gamepads[0].name, 'Xbox Controller');
      expect(gamepads[0].vendorId, 1118);
      expect(gamepads[1].id, 'test_1');
      expect(gamepads[1].name, 'DualSense');

      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(methodChannel, null);
    });

    test('listGamepads returns empty list when null', () async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(methodChannel, (call) async {
        if (call.method == 'listGamepads') return null;
        return null;
      });

      final gamepads = await platform.listGamepads();
      expect(gamepads, isEmpty);

      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(methodChannel, null);
    });
  });
}
