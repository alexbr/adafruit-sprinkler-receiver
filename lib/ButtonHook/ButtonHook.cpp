/**
 * Button helper.
 */

#include "ButtonHook.h"
#include <Arduino.h>

ButtonHook::ButtonHook() { this->buttons = new Button[0]; }

void ButtonHook::checkButtons() {
}

void ButtonHook::registerButton(int buttonPin) {
    int numButtons = sizeof(this->buttons);
    Button newButtons[numButtons + 1];
    memcpy(this->buttons, newButtons, numButtons);
    Button button;
    button.pin = buttonPin;
    newButtons[numButtons] = button;
    this->buttons = newButtons;
}