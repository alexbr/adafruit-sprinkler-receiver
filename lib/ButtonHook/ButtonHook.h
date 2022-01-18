#ifndef ButtonHook_h
#define ButtonHook_h

#include <string>
#include <vector>

/**
 * Callback handler for button press event. Parameter is true for long press and
 * false for short press.
 */
typedef void(*ButtonPressedCallback)(bool);

class Button {
  public:
    Button(int pin, ButtonPressedCallback callback);
    int pin;
    int state;
    unsigned long pressStart;
    ButtonPressedCallback callback;
};

class ButtonHook {
 private:
   unsigned long longPressDuration;
   std::vector<Button> buttons;

 public:
   ButtonHook(int longPressDuration);
   void checkButtons();
   void registerButton(int buttonPin, ButtonPressedCallback callback);
};

#endif