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

      // Button is active-low (INPUT_PULLUP), so LOW means pressed.
      if (buttonState == LOW && button.state == HIGH) {
         button.pressStart = millis();
         button.state = LOW;
         Serial.printf("button pin=%d pressed start=%lu\n", button.pin, button.pressStart);
      } else if (buttonState == HIGH && button.state == LOW) {
         unsigned long buttonDuration = millis() - button.pressStart;
         Serial.printf("button pin=%d released duration=%lu\n", button.pin, buttonDuration);
         button.callback(buttonDuration > this->longPressDuration);
         button.state = HIGH;
      }
   }
}

void ButtonHook::registerButton(int buttonPin, ButtonPressedCallback callback) {
   Button button(buttonPin, callback);
   this->buttons.push_back(button);
}