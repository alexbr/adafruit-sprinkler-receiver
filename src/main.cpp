/********************************************************

  Alex's sprinkler control

 *********************************************************/

#include "main.h"
#include "secrets.h"
#include "ButtonHook.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266Ping.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiUDP.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <Wire.h>

// Sprinkler state
enum SprinklerState { ON, OFF, UNKNOWN };
SprinklerState sprinklerState = UNKNOWN;
unsigned long sprinklerStart = 0;
unsigned long manualOverride = 0;
time_t sprinklerStartTime = 0;
char sprinklerStartDisplay[50] = "Start: N/A\n";
char sprinklerDurationDisplay[50] = "Run: N/A\n";

time_t lastRunStartTime = 0;
unsigned long lastRunDurationMs = 0;

// Internet setup
ESP8266WebServer server(80);
unsigned long checkServerTime = 0;
unsigned long lastHeapLog = 0; // for periodic heap logging

// Display
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);
unsigned long turnDisplayOff = 0;

// Button handling
ButtonHook buttonHook(LONG_PRESS);

// NTP
IPAddress timeServer(129, 6, 15, 28);
WiFiUDP ntpUdp;
NTPClient ntpClient(ntpUdp, timeServer);
unsigned long checkTimeAfter = 0;
TimeChangeRule myPST = {"PST", Second, Sun, Mar, 2, -7 * 60}; // Daylight time = UTC - 7 hours
TimeChangeRule myPDT = {"PDT", First, Sun, Nov, 2, -8 * 60};  // Standard time = UTC - 8 hours
Timezone myTZ(myPST, myPDT);

void setup() {
   Serial.begin(115200);

   pinMode(TRIGGER_PIN_ON, OUTPUT);
   pinMode(TRIGGER_PIN_OFF, OUTPUT);
   pinMode(BUTTON_A, INPUT_PULLUP);
   pinMode(BUTTON_B, INPUT_PULLUP);
   pinMode(BUTTON_C, INPUT_PULLUP);

   buttonHook.registerButton(BUTTON_A, &buttonAPressed);
   buttonHook.registerButton(BUTTON_C, &buttonCPressed);

   display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Address 0x3C for 128x32
   Serial.println("OLED begun");
   
   displayOnWithTimeout();
   
   display.dim(true);
   display.setTextSize(1);
   display.setTextColor(SSD1306_WHITE);
   printDisplay("connecting internet...");

   WiFi.mode(WIFI_STA);

   const char *ssid = SECRET_SSID;
   const char *passkey = SECRET_PASSKEY;

   printStringLn("connecting internet");
   WiFi.begin(ssid, passkey);
   
   while (!WiFi.isConnected()) {
      delay(500);
      printString(".");
   }
   printStringLn("");
   
   WiFi.setAutoReconnect(true);
   WiFi.setAutoConnect(true);
   
   printStringLn("internet initialized");
   char ipBuffer[32];
   snprintf(ipBuffer, sizeof(ipBuffer), "IP: %s", WiFi.localIP().toString().c_str());
   printDisplay(ipBuffer);

   server.on("/on", sprinklerOn);
   server.on("/off", sprinklerOff);
   server.on("/last_run", HTTP_GET, lastRunHandler);
   server.begin();

   printStringLn("HTTP server started");

   // NTP
   ntpClient.begin();
   Serial.println("ntp started");
   setSyncProvider(getTime);
   Serial.println("ntp done");
}

void loop() {
   buttonHook.checkButtons();
   
   if (millis() >= turnDisplayOff) {
      display.clearDisplay();
      display.display();
   }

   // Check if sprinkler override is on
   if (manualOverride != 0) {
      // If override has expired, turn the sprinkler off
      if ((long)(millis() - manualOverride) >= 0) {
         manualOverride = 0;
         sprinklerOff();
      }
   } else if (millis() > checkServerTime) {
      // If not, check the server to see if the sprinkler is scheduled to be on
      server.handleClient();
      checkServerTime = millis() + CHECK_SERVER_DELAY;
   }

   // Failsafe if sprinkler is running too long
   if (sprinklerState != OFF && (millis() - sprinklerStart) > SPRINKLER_ON_MAX) {
      printStringLn("sprinkler on past max threshold, turning off");
      sprinklerOff();
   }

   if (sprinklerState == ON) {
      char buffer[64];
      unsigned long duration = millis() - sprinklerStart;
      snprintf(buffer, sizeof(buffer), "Sprinkler running for\n%lu min(s)", duration / 60000UL);
      printDisplay(buffer);
   } else {
      char buffer[sizeof(sprinklerStartDisplay) + sizeof(sprinklerDurationDisplay) + 2];
      snprintf(buffer, sizeof(buffer), "%s\n%s", sprinklerStartDisplay, sprinklerDurationDisplay);
      printDisplay(buffer);
   }

   // Periodic heap logging to help diagnose fragmentation/leaks
   if (millis() - lastHeapLog >= 60000UL) {
      lastHeapLog = millis();
      Serial.printf("heap free=%u frag=%d\n", ESP.getFreeHeap(), ESP.getHeapFragmentation());
   }
}

void buttonAPressed(bool longPress) { displayOnWithTimeout(); }

void buttonCPressed(bool longPress) { toggleOverride(); }

void toggleOverride() {
   // Override is on, turn it off
   if ((long)(manualOverride - millis()) > 0) {
      manualOverride = 0;
      sprinklerOff();
   } else {
      manualOverride = millis() + MANUAL_OVERRIDE;
      displayOnWithTimeout();
      sprinklerOn();
   }
}

void sprinklerOn() {
   printStringLn("sprinkler on");
   if (sprinklerState == UNKNOWN || sprinklerState == OFF) {
      sprinklerState = ON;
      sprinklerStart = millis();
      time_t t = myTZ.toLocal(now());
      lastRunStartTime = t;
      snprintf(sprinklerStartDisplay, sizeof(sprinklerStartDisplay),
               "Start:\n%d-%02d-%02d %02d:%02d",
               year(t), month(t), day(t), hour(t), minute(t));
      printStringLn("turning sprinkler on");

      // pulse high for 10ms to latch
      digitalWrite(TRIGGER_PIN_ON, HIGH);
      delay(10);
      digitalWrite(TRIGGER_PIN_ON, LOW);
   }
}

void sprinklerOff() {
   printStringLn("sprinkler off");
   if (sprinklerState == UNKNOWN || sprinklerState == ON) {
      if (sprinklerState == ON) {
         unsigned long duration = millis() - sprinklerStart;
         lastRunDurationMs = duration;
         snprintf(sprinklerDurationDisplay, sizeof(sprinklerDurationDisplay),
                  "Duration:\n%lu min(s)", duration / 60000UL);
      }
      
      sprinklerState = OFF;
      sprinklerStart = 0;
      printStringLn("turning sprinkler off");

      // pulse high for 10ms to latch
      digitalWrite(TRIGGER_PIN_OFF, HIGH);
      delay(10);
      digitalWrite(TRIGGER_PIN_OFF, LOW);
   }
}

void displayOnWithTimeout() {
   turnDisplayOff = millis() + DISPLAY_TIMEOUT;
}

void printDisplay(const char *str) {
   display.clearDisplay();
   
   if (turnDisplayOff <= millis()) {
      display.display();
      return;
   }
   
   display.setCursor(0,0);
   display.println(str);
   display.display();
   display.setCursor(0,0);
}

void printDisplay(String str) {
   printDisplay(str.c_str());
}

time_t getTime() {
   return ntpClient.getTime();
}

void lastRunHandler() {
   if (lastRunStartTime == 0) {
      server.send(204, "application/json", "{}");
      return;
   }

   time_t t = lastRunStartTime;
   char timebuf[32];
   snprintf(timebuf, sizeof(timebuf), "%04d-%02d-%02d %02d:%02d",
            year(t), month(t), day(t), hour(t), minute(t));

   unsigned long mins = lastRunDurationMs / 60000UL;
   char jsonbuf[128];
   snprintf(jsonbuf, sizeof(jsonbuf),
            "{\"start\":\"%s\",\"duration_min\":%lu}",
            timebuf, mins);

   server.send(200, "application/json", jsonbuf);
}

void printString(const char *str) {
   const char *p = str;
   while (*p) {
      Serial.print(*p);
      p++;
   }
}

void printStringLn(const char *str) {
   printString(str);
   Serial.print('\n');
}
