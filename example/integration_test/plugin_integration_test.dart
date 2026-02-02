// This is a basic Flutter integration test.
//
// Since integration tests run in a full Flutter application, they can interact
// with the host side of a plugin implementation, unlike Dart unit tests.
//
// For more information about Flutter integration tests, please see
// https://flutter.dev/to/integration-testing


import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

import 'package:universal_gamepad/universal_gamepad.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('listGamepads returns a list', (WidgetTester tester) async {
    final gamepads = await Gamepad.instance.listGamepads();
    // May be empty if no gamepad is connected, but should not throw.
    expect(gamepads, isA<List<GamepadInfo>>());
  });
}
