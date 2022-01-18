/**
 * Button helper.
 */

#include "ButtonHook.h"
#include <Arduino.h>
#include <iterator>

Button::Button(int pin, ButtonPressedCallback callback) {
    this->pin = pin;
    this->callback = callback;
    this->state = HIGH;
}

ButtonHook::ButtonHook(int longPressDuration) {
   this->buttons = {};
   this->longPressDuration = longPressDuration;
}

void ButtonHook::checkButtons() {
   std::vector<Button>::iterator it;
   for (it = this->buttons.begin(); it != this->buttons.end(); ++it) {
      Button& button = *it;

      int buttonState = digitalRead(button.pin);

      // Button pressed
      if (buttonState == LOW) {
         // Switch from unpressed to pressed
         if (buttonState != button.state) {
            button.pressStart = millis();
         }
      } else {
         if (buttonState != button.state) {
            // Switch from pressed to not pressed
            unsigned long buttonDuration = millis() - button.pressStart;

            if (buttonDuration > this->longPressDuration) {
               button.callback(/* longPress= */ true);
            } else {
               button.callback(/* longPress= */ false);
            }
         }
      }

      button.state = buttonState;
   }
}

void ButtonHook::registerButton(int buttonPin, ButtonPressedCallback callback) {
   Button button(buttonPin, callback);
   this->buttons.push_back(button);
}