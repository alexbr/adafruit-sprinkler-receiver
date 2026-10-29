/********************************************************

  Alex's sprinkler control

 *********************************************************/

#include "main.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <Wire.h>

#include "ButtonHook.h"
#include "secrets.h"

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
WebServer server(80);
unsigned long checkServerTime = 0;
unsigned long lastHeapLog = 0;  // for periodic heap logging

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
TimeChangeRule myPST = {"PST", Second, Sun, Mar,
                        2,     -7 * 60};  // Daylight time = UTC - 7 hours
TimeChangeRule myPDT = {"PDT", First,  Sun, Nov,
                        2,     -8 * 60};  // Standard time = UTC - 8 hours
Timezone myTZ(myPST, myPDT);

static void setSprinklerOn();
static void setSprinklerOff();
static void pulseLatch(int pin);

void setup() {
  Serial.begin(115200);

  // Setup relay pins
  pinMode(TRIGGER_PIN_ON, OUTPUT);
  pinMode(TRIGGER_PIN_OFF, OUTPUT);
  pulseLatch(TRIGGER_PIN_OFF);

  // Setup buttons
  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);

  buttonHook.registerButton(BUTTON_A, &buttonAPressed);
  buttonHook.registerButton(BUTTON_B, &buttonBPressed);
  buttonHook.registerButton(BUTTON_C, &buttonCPressed);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // Address 0x3C for 128x32
  Serial.println("OLED begun");

  displayOnWithTimeout();

  display.dim(true);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  printDisplay("connecting internet...");

  WiFi.mode(WIFI_STA);

  const char* ssid = SECRET_SSID;
  const char* passkey = SECRET_PASSKEY;

  printStringLn("connecting internet");
  WiFi.begin(ssid, passkey);

  while (!WiFi.isConnected()) {
    delay(500);
    printString(".");
  }
  printStringLn("");

  WiFi.setAutoReconnect(true);

  printStringLn("internet initialized");
  char ipBuffer[32];
  snprintf(ipBuffer, sizeof(ipBuffer), "IP: %s",
           WiFi.localIP().toString().c_str());
  printDisplay(ipBuffer);

  server.on("/on", sprinklerOnWebHandler);
  server.on("/off", sprinklerOffWebHandler);
  server.on("/", HTTP_GET, infoHandler);
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

  // If sprinkler override is on and has expired, turn the sprinkler off
  if (manualOverride != 0 && (long)(millis() - manualOverride) >= 0) {
    manualOverride = 0;
    setSprinklerOff();
  }

  if (millis() > checkServerTime) {
    server.handleClient();
    checkServerTime = millis() + CHECK_SERVER_DELAY;
  }

  // Failsafe if sprinkler is running too long
  if (sprinklerState != OFF && (millis() - sprinklerStart) > SPRINKLER_ON_MAX) {
    printStringLn("sprinkler on past max threshold, turning off");
    setSprinklerOff();
  }

  // Display sprinkler state
  if (sprinklerState == ON) {
    char buffer[64];
    unsigned long duration = millis() - sprinklerStart;
    snprintf(buffer, sizeof(buffer), "Sprinkler running for\n%.2f min(s)",
             duration / 60000.0);
    printDisplay(buffer);
  } else {
    char buffer[sizeof(sprinklerStartDisplay) +
                sizeof(sprinklerDurationDisplay) + 2];
    snprintf(buffer, sizeof(buffer), "%s\n%s", sprinklerStartDisplay,
             sprinklerDurationDisplay);
    printDisplay(buffer);
  }

  // Periodic heap logging to help diagnose fragmentation/leaks
  if (millis() - lastHeapLog >= 60000UL) {
    lastHeapLog = millis();
    Serial.printf("heap free=%lu\n", (unsigned long)ESP.getFreeHeap());
  }
}

void buttonAPressed(bool longPress) { sprinklerOnButtonHandler(longPress); }

void buttonBPressed(bool longPress) { sprinklerOffButtonHandler(); }

void buttonCPressed(bool longPress) { displayOnWithTimeout(); }

void sprinklerOnButtonHandler(bool longPress) {
  // Set the override timeout and turn on the sprinkler
  if (longPress) {
    printStringLn("running sprinkler for 60 mins");
    manualOverride = millis() + MANUAL_OVERRIDE_60;
  } else {
    printStringLn("running sprinkler for 30 mins");
    manualOverride = millis() + MANUAL_OVERRIDE_30;
  }
  
  setSprinklerOn();
}

void sprinklerOffButtonHandler() {
  // Reset the override timeout and turn off the sprinkler
  manualOverride = 0;
  setSprinklerOff();
}

void sprinklerOnWebHandler() {
  if (manualOverride != 0) {
    server.send(200, "application/json", "{\"status\":\"override\"}");
    return;
  }

  setSprinklerOn();
  server.send(200, "application/json", "{\"status\":\"on\"}");
}

void sprinklerOffWebHandler() {
  if (manualOverride != 0) {
    server.send(200, "application/json", "{\"status\":\"override\"}");
    return;
  }

  setSprinklerOff();
  server.send(200, "application/json", "{\"status\":\"off\"}");
}

static void pulseLatch(int pin) {
  digitalWrite(pin, HIGH);
  delay(12);
  digitalWrite(pin, LOW);
}

static void setSprinklerOn() {
  if (sprinklerState == ON) {
    printStringLn("sprinkler already on");
    return;
  }

  printStringLn("turning sprinkler on");
  sprinklerState = ON;
  sprinklerStart = millis();
  time_t t = myTZ.toLocal(now());
  lastRunStartTime = t;

  snprintf(sprinklerStartDisplay, sizeof(sprinklerStartDisplay),
           "Start:\n%d-%02d-%02d %02d:%02d", year(t), month(t), day(t), hour(t),
           minute(t));
  displayOnWithTimeout();
  pulseLatch(TRIGGER_PIN_ON);
}

static void setSprinklerOff() {
  printStringLn("turning sprinkler off");

  if (sprinklerState == ON) {
    unsigned long duration = millis() - sprinklerStart;
    lastRunDurationMs = duration;
    sprinklerState = OFF;
    sprinklerStart = 0;

    snprintf(sprinklerDurationDisplay, sizeof(sprinklerDurationDisplay),
             "Duration:\n%.1f min(s)", duration / 60000.0);

    displayOnWithTimeout();
  }

  // Always turn off to be safe
  pulseLatch(TRIGGER_PIN_OFF);
}

void infoHandler() {
  printStringLn("returning info");
  if (lastRunStartTime == 0) {
    server.send(200, "application/json", "{}");
    return;
  }

  time_t t = lastRunStartTime;
  char timebuf[32];
  snprintf(timebuf, sizeof(timebuf), "%04d-%02d-%02d %02d:%02d", year(t),
           month(t), day(t), hour(t), minute(t));

  char statusBuf[9];

  if (manualOverride != 0) {
    sprintf(statusBuf, "override");
  } else if (sprinklerState == ON) {
    sprintf(statusBuf, "on");
  } else {
    sprintf(statusBuf, "off");
  }

  unsigned long duration;
  if (sprinklerState == ON) {
    duration = millis() - sprinklerStart;
  } else {
    duration = lastRunDurationMs;
  }

  char jsonbuf[128];
  snprintf(jsonbuf, sizeof(jsonbuf),
           "{\"start\":\"%s\",\"duration_ms\":%lu,\"status\":\"%s\"}", timebuf,
           duration, statusBuf);

  server.send(200, "application/json", jsonbuf);
}

void displayOnWithTimeout() { turnDisplayOff = millis() + DISPLAY_TIMEOUT; }

void printDisplay(const char* str) {
  display.clearDisplay();

  if (turnDisplayOff <= millis()) {
    display.display();
    return;
  }

  display.setCursor(0, 0);
  display.println(str);
  display.display();
  display.setCursor(0, 0);
}

void printDisplay(String str) { printDisplay(str.c_str()); }

time_t getTime() { return ntpClient.getTime(); }

void printString(const char* str) {
  const char* p = str;
  while (*p) {
    Serial.print(*p);
    p++;
  }
}

void printStringLn(const char* str) {
  printString(str);
  Serial.print('\n');
}
