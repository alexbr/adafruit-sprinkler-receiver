#ifndef main_h_
#define main_h_

#include <Arduino.h>
#include <TimeLib.h>


#define TRIGGER_PIN_ON 13 // For 24V relay switching
#define TRIGGER_PIN_OFF 15 // For 24V relay switching

// For OLED
#define BUTTON_A 0
#define BUTTON_B 16
#define BUTTON_C 2

const int CHECK_TIME_DELAY_MS = 300000; // 5 mins
const int SPRINKLER_ON_MAX = 60 * 60000; // 1 hour

// Loop functions
void checkButtons();
    
// Helpers
void sprinklerOn();
void sprinklerOff();
time_t getTime();
void printDisplay(String str);
void printString(const char *str);
void printStringLn(const char *str);

#endif