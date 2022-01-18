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

// Internet setup
ESP8266WebServer server(80);
unsigned long checkServerTime = 0;

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
   printDisplay("IP: " + WiFi.localIP().toString());

   server.on("/on", sprinklerOn);
   server.on("/off", sprinklerOff);
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
      if (manualOverride < millis()) {
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
      const char* format = "Sprinkler running for\n%lu min(s)";
      char buffer[strlen(format) + 6];
      long duration = millis() - sprinklerStart;
      sprintf(buffer, "Sprinkler running for\n%.1f min(s)", duration / 60000.0);
      printDisplay(buffer);
   } else {
      char buffer[strlen(sprinklerStartDisplay) + strlen(sprinklerDurationDisplay) + 1];
      sprintf(buffer, "%s\n%s", sprinklerStartDisplay, sprinklerDurationDisplay);
      printDisplay(buffer);
   }
}

void buttonAPressed(bool longPress) { displayOnWithTimeout(); }

void buttonCPressed(bool longPress) { toggleOverride(); }

void toggleOverride() {
   // Override is on, turn it off
   if (millis() < manualOverride) {
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
      sprintf(sprinklerStartDisplay, "Start:\n%d-%02d-%02d %02d:%02d", year(t),
              month(t), day(t), hour(t), minute(t));
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
         long duration = millis() - sprinklerStart;
         sprintf(sprinklerDurationDisplay, "Duration:\n%.1f min(s)", duration / 60000.0);
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

void printDisplay(String str) {
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

time_t getTime() {
   return ntpClient.getTime();
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