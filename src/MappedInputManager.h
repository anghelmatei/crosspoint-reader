#pragma once

#include <HalGPIO.h>

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  enum class PowerGesture { None, SingleTap, DoubleTap };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;

  // Must be called once per main loop after gpio.update().
  void updateGestures();

  // Consume queued gesture events.
  bool consumePowerSingleTap();
  bool consumePowerDoubleTap();

  // Clears any pending/queued gesture state (e.g., ignore first release after wake).
  void resetPowerGestures();

 private:
  HalGPIO& gpio;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;

  // Gesture state
  unsigned long powerFirstTapMs = 0;
  bool powerWaitingForSecondTap = false;
  bool powerSingleTapQueued = false;
  bool powerDoubleTapQueued = false;
};
