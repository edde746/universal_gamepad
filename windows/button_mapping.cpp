#include "button_mapping.h"

#include <algorithm>
#include <cmath>

namespace gamepad {

int XInputButtonToW3C(WORD xinput_button) {
  switch (xinput_button) {
    case XINPUT_GAMEPAD_A:
      return W3CButton::kA;
    case XINPUT_GAMEPAD_B:
      return W3CButton::kB;
    case XINPUT_GAMEPAD_X:
      return W3CButton::kX;
    case XINPUT_GAMEPAD_Y:
      return W3CButton::kY;
    case XINPUT_GAMEPAD_LEFT_SHOULDER:
      return W3CButton::kLeftShoulder;
    case XINPUT_GAMEPAD_RIGHT_SHOULDER:
      return W3CButton::kRightShoulder;
    case XINPUT_GAMEPAD_BACK:
      return W3CButton::kBack;
    case XINPUT_GAMEPAD_START:
      return W3CButton::kStart;
    case XINPUT_GAMEPAD_LEFT_THUMB:
      return W3CButton::kLeftStickButton;
    case XINPUT_GAMEPAD_RIGHT_THUMB:
      return W3CButton::kRightStickButton;
    case XINPUT_GAMEPAD_DPAD_UP:
      return W3CButton::kDpadUp;
    case XINPUT_GAMEPAD_DPAD_DOWN:
      return W3CButton::kDpadDown;
    case XINPUT_GAMEPAD_DPAD_LEFT:
      return W3CButton::kDpadLeft;
    case XINPUT_GAMEPAD_DPAD_RIGHT:
      return W3CButton::kDpadRight;
    default:
      return -1;
  }
}

std::vector<WORD> GetAllXInputDigitalButtons() {
  return {
      XINPUT_GAMEPAD_A,
      XINPUT_GAMEPAD_B,
      XINPUT_GAMEPAD_X,
      XINPUT_GAMEPAD_Y,
      XINPUT_GAMEPAD_LEFT_SHOULDER,
      XINPUT_GAMEPAD_RIGHT_SHOULDER,
      XINPUT_GAMEPAD_BACK,
      XINPUT_GAMEPAD_START,
      XINPUT_GAMEPAD_LEFT_THUMB,
      XINPUT_GAMEPAD_RIGHT_THUMB,
      XINPUT_GAMEPAD_DPAD_UP,
      XINPUT_GAMEPAD_DPAD_DOWN,
      XINPUT_GAMEPAD_DPAD_LEFT,
      XINPUT_GAMEPAD_DPAD_RIGHT,
  };
}

double NormalizeThumbstick(SHORT value, SHORT dead_zone) {
  double v = static_cast<double>(value);
  double dz = static_cast<double>(dead_zone);

  if (std::abs(v) <= dz) {
    return 0.0;
  }

  // Map the range [dead_zone, 32767] to [0.0, 1.0] (positive)
  // and [-32768, -dead_zone] to [-1.0, 0.0] (negative).
  double max_val = (v > 0) ? 32767.0 : 32768.0;
  double sign = (v > 0) ? 1.0 : -1.0;
  double abs_v = std::abs(v);

  double normalized = sign * ((abs_v - dz) / (max_val - dz));
  return std::clamp(normalized, -1.0, 1.0);
}

double NormalizeTrigger(BYTE value, BYTE threshold) {
  if (value <= threshold) {
    return 0.0;
  }

  double v = static_cast<double>(value);
  double t = static_cast<double>(threshold);

  double normalized = (v - t) / (255.0 - t);
  return std::clamp(normalized, 0.0, 1.0);
}

}  // namespace gamepad
