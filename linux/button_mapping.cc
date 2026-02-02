#include "button_mapping.h"

#if HAVE_SDL3

namespace ButtonMapping {

int SdlButtonToW3C(SDL_GamepadButton button) {
  switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
      return kButtonA;
    case SDL_GAMEPAD_BUTTON_EAST:
      return kButtonB;
    case SDL_GAMEPAD_BUTTON_WEST:
      return kButtonX;
    case SDL_GAMEPAD_BUTTON_NORTH:
      return kButtonY;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
      return kLeftShoulder;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
      return kRightShoulder;
    case SDL_GAMEPAD_BUTTON_BACK:
      return kBack;
    case SDL_GAMEPAD_BUTTON_START:
      return kStart;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
      return kLeftStickButton;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
      return kRightStickButton;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
      return kDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
      return kDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
      return kDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
      return kDpadRight;
    case SDL_GAMEPAD_BUTTON_GUIDE:
      return kGuide;
    default:
      return -1;
  }
}

int SdlAxisToW3C(SDL_GamepadAxis axis) {
  switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX:
      return kLeftStickX;
    case SDL_GAMEPAD_AXIS_LEFTY:
      return kLeftStickY;
    case SDL_GAMEPAD_AXIS_RIGHTX:
      return kRightStickX;
    case SDL_GAMEPAD_AXIS_RIGHTY:
      return kRightStickY;
    default:
      // Trigger axes are not standard axes; they are treated as buttons.
      return -1;
  }
}

bool IsTriggerAxis(SDL_GamepadAxis axis) {
  return axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
         axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
}

int TriggerAxisToButtonIndex(SDL_GamepadAxis axis) {
  if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
    return kLeftTrigger;
  }
  if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
    return kRightTrigger;
  }
  return -1;
}

double NormalizeStickAxis(int16_t value) {
  // SDL stick axes range from -32768 to 32767.
  // Clamp -32768 to -32767 so the range is symmetric, then divide.
  if (value < -32767) {
    value = -32767;
  }
  return static_cast<double>(value) / 32767.0;
}

double NormalizeTriggerAxis(int16_t value) {
  // SDL trigger axes range from 0 to 32767.
  if (value < 0) {
    value = 0;
  }
  return static_cast<double>(value) / 32767.0;
}

}  // namespace ButtonMapping

#endif  // HAVE_SDL3
