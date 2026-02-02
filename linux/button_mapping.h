#ifndef BUTTON_MAPPING_H_
#define BUTTON_MAPPING_H_

#include <cstdint>

#if HAVE_SDL3
#include <SDL3/SDL.h>
#endif

/// Maps SDL3 gamepad buttons and axes to W3C Standard Gamepad indices.
///
/// W3C Standard Gamepad button mapping:
///   0 = a (bottom), 1 = b (right), 2 = x (left), 3 = y (top)
///   4 = leftShoulder, 5 = rightShoulder
///   6 = leftTrigger, 7 = rightTrigger
///   8 = back, 9 = start
///   10 = leftStickButton, 11 = rightStickButton
///   12 = dpadUp, 13 = dpadDown, 14 = dpadLeft, 15 = dpadRight
///   16 = guide
///
/// W3C Standard Gamepad axis mapping:
///   0 = leftStickX, 1 = leftStickY, 2 = rightStickX, 3 = rightStickY
///
/// SDL3 trigger axes (SDL_GAMEPAD_AXIS_LEFT_TRIGGER and
/// SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) are mapped to button indices 6 and 7
/// respectively, since the W3C spec treats triggers as analog buttons.
namespace ButtonMapping {

// W3C button indices.
constexpr int kButtonA = 0;
constexpr int kButtonB = 1;
constexpr int kButtonX = 2;
constexpr int kButtonY = 3;
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

// W3C axis indices.
constexpr int kLeftStickX = 0;
constexpr int kLeftStickY = 1;
constexpr int kRightStickX = 2;
constexpr int kRightStickY = 3;

#if HAVE_SDL3

/// Maps an SDL_GamepadButton to its W3C Standard Gamepad button index.
/// Returns -1 if the button has no standard mapping.
int SdlButtonToW3C(SDL_GamepadButton button);

/// Maps an SDL_GamepadAxis to its W3C Standard Gamepad axis index.
/// Returns -1 if the axis is a trigger (triggers are treated as buttons).
int SdlAxisToW3C(SDL_GamepadAxis axis);

/// Returns true if the given SDL axis is a trigger axis.
bool IsTriggerAxis(SDL_GamepadAxis axis);

/// Returns the W3C button index for the given trigger axis.
/// Only valid when IsTriggerAxis() returns true.
int TriggerAxisToButtonIndex(SDL_GamepadAxis axis);

/// Normalizes an SDL stick axis value (-32768..32767) to -1.0..1.0.
double NormalizeStickAxis(int16_t value);

/// Normalizes an SDL trigger axis value (0..32767) to 0.0..1.0.
double NormalizeTriggerAxis(int16_t value);

#endif  // HAVE_SDL3

}  // namespace ButtonMapping

#endif  // BUTTON_MAPPING_H_
