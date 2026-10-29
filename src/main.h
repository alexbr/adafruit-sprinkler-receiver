#ifndef main_h_
#define main_h_

#include <Arduino.h>
#include <TimeLib.h>

#define TRIGGER_PIN_ON 13 // For 24V relay switching
#define TRIGGER_PIN_OFF 27 // For 24V relay switching

// Actual Feather board button pins used by the hardware.
#define BUTTON_A 15
#define BUTTON_B 32 
#define BUTTON_C 14 

const int CHECK_TIME_DELAY_MS = 5 * 60 * 1000; // 5 mins
const int CHECK_SERVER_DELAY = 500;
const int SPRINKLER_ON_MAX = 60 * 60000; // 1 hour
const int DISPLAY_TIMEOUT = 60 * 1000; // 1 min
const int LONG_PRESS = 2 * 1000; // 2s
const int MANUAL_OVERRIDE_30 = 30 * 60 * 1000; // 30 mins
const int MANUAL_OVERRIDE_60 = 60 * 60 * 1000; // 60 mins
    
// Helpers
void sprinklerOnWebHandler();
void sprinklerOffWebHandler();
void sprinklerOnButtonHandler(bool longPress);
void sprinklerOffButtonHandler();
void infoHandler();
void toggleOverride();
time_t getTime();
void buttonAPressed(bool);
void buttonBPressed(bool);
void buttonCPressed(bool);
void displayOnWithTimeout();
void printDisplay(String str);
void printString(const char *str);
void printStringLn(const char *str);

#endif