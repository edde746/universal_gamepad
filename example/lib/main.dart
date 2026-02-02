import 'dart:async';

import 'package:flutter/material.dart';
import 'package:gamepad/gamepad.dart';

void main() {
  runApp(const GamepadDemoApp());
}

class GamepadDemoApp extends StatelessWidget {
  const GamepadDemoApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Gamepad Demo',
      theme: ThemeData(
        colorSchemeSeed: Colors.deepPurple,
        useMaterial3: true,
        brightness: Brightness.dark,
      ),
      home: const GamepadDemoPage(),
    );
  }
}

class GamepadDemoPage extends StatefulWidget {
  const GamepadDemoPage({super.key});

  @override
  State<GamepadDemoPage> createState() => _GamepadDemoPageState();
}

class _GamepadDemoPageState extends State<GamepadDemoPage> {
  final _gamepads = Gamepad.instance;
  StreamSubscription<GamepadEvent>? _subscription;

  final Map<String, GamepadInfo> _connectedGamepads = {};
  String? _selectedGamepadId;

  // Button states: button index -> value (0.0-1.0)
  final Map<int, double> _buttonStates = {};
  // Axis states: axis index -> value (-1.0 to 1.0)
  final Map<int, double> _axisStates = {};
  // Event log
  final List<String> _eventLog = [];
  static const _maxLogEntries = 50;

  @override
  void initState() {
    super.initState();
    _startListening();
    _loadGamepads();
  }

  Future<void> _loadGamepads() async {
    final gamepads = await _gamepads.listGamepads();
    if (!mounted) return;
    setState(() {
      for (final gp in gamepads) {
        _connectedGamepads[gp.id] = gp;
      }
      if (_selectedGamepadId == null && _connectedGamepads.isNotEmpty) {
        _selectedGamepadId = _connectedGamepads.keys.first;
      }
    });
  }

  void _startListening() {
    _subscription = _gamepads.events.listen(_onEvent);
  }

  void _onEvent(GamepadEvent event) {
    setState(() {
      switch (event) {
        case GamepadConnectionEvent e:
          if (e.connected) {
            _connectedGamepads[e.gamepadId] = e.info;
            _selectedGamepadId ??= e.gamepadId;
            _addLog('Connected: ${e.info.name}');
          } else {
            _connectedGamepads.remove(e.gamepadId);
            if (_selectedGamepadId == e.gamepadId) {
              _selectedGamepadId = _connectedGamepads.keys.firstOrNull;
              _buttonStates.clear();
              _axisStates.clear();
            }
            _addLog('Disconnected: ${e.info.name}');
          }

        case GamepadButtonEvent e:
          if (e.gamepadId == _selectedGamepadId) {
            _buttonStates[e.button.index] = e.value;
          }
          _addLog(
            '${e.button.name}: ${e.pressed ? "pressed" : "released"} '
            '(${e.value.toStringAsFixed(2)})',
          );

        case GamepadAxisEvent e:
          if (e.gamepadId == _selectedGamepadId) {
            _axisStates[e.axis.index] = e.value;
          }
          _addLog(
            '${e.axis.name}: ${e.value.toStringAsFixed(3)}',
          );
      }
    });
  }

  void _addLog(String message) {
    _eventLog.insert(0, message);
    if (_eventLog.length > _maxLogEntries) {
      _eventLog.removeLast();
    }
  }

  @override
  void dispose() {
    _subscription?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Gamepad Demo'),
        actions: [
          if (_connectedGamepads.length > 1)
            DropdownButton<String>(
              value: _selectedGamepadId,
              items: _connectedGamepads.entries
                  .map((e) => DropdownMenuItem(
                        value: e.key,
                        child: Text(e.value.name),
                      ))
                  .toList(),
              onChanged: (id) => setState(() {
                _selectedGamepadId = id;
                _buttonStates.clear();
                _axisStates.clear();
              }),
            ),
        ],
      ),
      body: _connectedGamepads.isEmpty
          ? const Center(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(Icons.gamepad, size: 64, color: Colors.grey),
                  SizedBox(height: 16),
                  Text(
                    'No gamepads connected',
                    style: TextStyle(fontSize: 18, color: Colors.grey),
                  ),
                  SizedBox(height: 8),
                  Text(
                    'Connect a gamepad and press any button',
                    style: TextStyle(color: Colors.grey),
                  ),
                ],
              ),
            )
          : _buildGamepadView(),
    );
  }

  Widget _buildGamepadView() {
    return LayoutBuilder(
      builder: (context, constraints) {
        if (constraints.maxWidth > 800) {
          return Row(
            children: [
              Expanded(flex: 3, child: _buildInputDisplay()),
              Expanded(flex: 2, child: _buildEventLog()),
            ],
          );
        }
        return Column(
          children: [
            Expanded(flex: 3, child: _buildInputDisplay()),
            Expanded(flex: 2, child: _buildEventLog()),
          ],
        );
      },
    );
  }

  Widget _buildInputDisplay() {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (_selectedGamepadId != null &&
              _connectedGamepads[_selectedGamepadId] != null)
            Padding(
              padding: const EdgeInsets.only(bottom: 16),
              child: Text(
                _connectedGamepads[_selectedGamepadId]!.name,
                style: Theme.of(context).textTheme.titleLarge,
              ),
            ),
          _buildStickSection(),
          const SizedBox(height: 24),
          _buildButtonSection(),
        ],
      ),
    );
  }

  Widget _buildStickSection() {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceEvenly,
      children: [
        _buildStickWidget(
          'Left Stick',
          _axisStates[GamepadAxis.leftStickX.index] ?? 0.0,
          _axisStates[GamepadAxis.leftStickY.index] ?? 0.0,
          _buttonStates[GamepadButton.leftStickButton.index] ?? 0.0,
        ),
        _buildStickWidget(
          'Right Stick',
          _axisStates[GamepadAxis.rightStickX.index] ?? 0.0,
          _axisStates[GamepadAxis.rightStickY.index] ?? 0.0,
          _buttonStates[GamepadButton.rightStickButton.index] ?? 0.0,
        ),
      ],
    );
  }

  Widget _buildStickWidget(
    String label,
    double x,
    double y,
    double pressed,
  ) {
    const size = 120.0;
    return Column(
      children: [
        Text(label, style: Theme.of(context).textTheme.labelLarge),
        const SizedBox(height: 4),
        Container(
          width: size,
          height: size,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            color: pressed > 0.5
                ? Colors.deepPurple.withValues(alpha: 0.3)
                : Colors.grey.withValues(alpha: 0.2),
            border: Border.all(color: Colors.grey),
          ),
          child: CustomPaint(
            painter: _CrosshairPainter(x: x, y: y),
          ),
        ),
        const SizedBox(height: 4),
        Text(
          '(${x.toStringAsFixed(2)}, ${y.toStringAsFixed(2)})',
          style: Theme.of(context).textTheme.bodySmall,
        ),
      ],
    );
  }

  Widget _buildButtonSection() {
    return Wrap(
      spacing: 8,
      runSpacing: 8,
      children: GamepadButton.values.map((button) {
        final value = _buttonStates[button.index] ?? 0.0;
        final isPressed = value > 0.1;
        return _buildButtonChip(button.name, value, isPressed);
      }).toList(),
    );
  }

  Widget _buildButtonChip(String label, double value, bool isPressed) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(8),
        color: isPressed
            ? Color.lerp(
                Colors.deepPurple.withValues(alpha: 0.3),
                Colors.deepPurple,
                value,
              )
            : Colors.grey.withValues(alpha: 0.15),
        border: Border.all(
          color: isPressed ? Colors.deepPurple : Colors.grey.withValues(alpha: 0.3),
        ),
      ),
      child: Text(
        label,
        style: TextStyle(
          color: isPressed ? Colors.white : Colors.grey,
          fontWeight: isPressed ? FontWeight.bold : FontWeight.normal,
        ),
      ),
    );
  }

  Widget _buildEventLog() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              Text(
                'Event Log',
                style: Theme.of(context).textTheme.titleMedium,
              ),
              const Spacer(),
              TextButton(
                onPressed: () => setState(() => _eventLog.clear()),
                child: const Text('Clear'),
              ),
            ],
          ),
        ),
        Expanded(
          child: ListView.builder(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            itemCount: _eventLog.length,
            itemBuilder: (context, index) {
              return Padding(
                padding: const EdgeInsets.only(bottom: 2),
                child: Text(
                  _eventLog[index],
                  style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        fontFamily: 'monospace',
                      ),
                ),
              );
            },
          ),
        ),
      ],
    );
  }
}

class _CrosshairPainter extends CustomPainter {
  const _CrosshairPainter({required this.x, required this.y});

  final double x;
  final double y;

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final radius = size.width / 2 - 4;

    // Draw crosshair lines
    final linePaint = Paint()
      ..color = Colors.grey.withValues(alpha: 0.3)
      ..strokeWidth = 1;
    canvas.drawLine(
      Offset(center.dx, 4),
      Offset(center.dx, size.height - 4),
      linePaint,
    );
    canvas.drawLine(
      Offset(4, center.dy),
      Offset(size.width - 4, center.dy),
      linePaint,
    );

    // Draw indicator dot
    final dotX = center.dx + x * radius;
    final dotY = center.dy + y * radius;
    final dotPaint = Paint()..color = Colors.deepPurple;
    canvas.drawCircle(Offset(dotX, dotY), 6, dotPaint);
  }

  @override
  bool shouldRepaint(_CrosshairPainter oldDelegate) =>
      x != oldDelegate.x || y != oldDelegate.y;
}
