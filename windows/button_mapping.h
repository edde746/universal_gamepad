#ifndef FLUTTER_PLUGIN_BUTTON_MAPPING_H_
#define FLUTTER_PLUGIN_BUTTON_MAPPING_H_

#include <windows.h>
#include <xinput.h>

#include <cstdint>
#include <vector>

namespace gamepad {

/// W3C Standard Gamepad button indices.
namespace W3CButton {
constexpr int kA = 0;
constexpr int kB = 1;
constexpr int kX = 2;
constexpr int kY = 3;
constexpr int kLeftShoulder = 4;
constexpr int kRightShoulder = 5;
constexpr int kLeftTrigger = 6;
constexpr int kRightTrigger = 7;
constexpr int kBack = 8;
constexpr int kStart = 9;
constexpr int kLeftStickButton = 10;
constexpr int kRightStickButton = 11;
constexpr int kDpadUp = 12;
constexpr int kDpadDown = 13;
constexpr int kDpadLeft = 14;
constexpr int kDpadRight = 15;
constexpr int kGuide = 16;
constexpr int kCount = 17;
}  // namespace W3CButton

/// W3C Standard Gamepad axis indices.
namespace W3CAxis {
constexpr int kLeftStickX = 0;
constexpr int kLeftStickY = 1;
constexpr int kRightStickX = 2;
constexpr int kRightStickY = 3;
constexpr int kCount = 4;
}  // namespace W3CAxis

/// Maps an XInput digital button bitmask flag to its W3C button index.
/// Returns -1 if the flag does not map to a known button.
int XInputButtonToW3C(WORD xinput_button);

/// Returns a list of all XInput digital button bitmask values that we map.
std::vector<WORD> GetAllXInputDigitalButtons();

/// Normalizes a thumbstick axis value (SHORT, -32768..32767) to -1.0..1.0,
/// applying the given dead zone. Values within the dead zone map to 0.0.
double NormalizeThumbstick(SHORT value, SHORT dead_zone);

/// Normalizes a trigger value (BYTE, 0..255) to 0.0..1.0,
/// applying the given threshold. Values at or below the threshold map to 0.0.
double NormalizeTrigger(BYTE value, BYTE threshold);

}  // namespace gamepad

#endif  // FLUTTER_PLUGIN_BUTTON_MAPPING_H_
