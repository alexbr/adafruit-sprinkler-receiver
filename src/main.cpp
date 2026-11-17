/********************************************************

  Alex's sprinkler control

 *********************************************************/

#include "main.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <Wire.h>
#include <string.h>

#include "ButtonHook.h"
#include "secrets.h"

// Sprinkler state
enum SprinklerState { ON, OFF, UNKNOWN };
enum IrrigationRunType { AUTOMATIC, MANUAL };
SprinklerState sprinklerState = UNKNOWN;
IrrigationRunType currentRunType = AUTOMATIC;
unsigned long sprinklerStart = 0;
unsigned long manualOverride = 0;
time_t sprinklerStartTime = 0;
char sprinklerStartDisplay[50] = "Hello\n";
char sprinklerDurationDisplay[50] = "Rodriguez!";

time_t lastRunStartTime = 0;
time_t lastRunStartEpoch = 0;
unsigned long lastRunDurationMs = 0;
IrrigationRunType lastRunType = AUTOMATIC;

// Internet setup
WebServer server(80);
unsigned long checkServerTime = 0;
unsigned long lastHeapLog = 0;  // for periodic heap logging

// Display
Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);
bool displayInfoOverride = true;
unsigned long turnDisplayOff = 0;
static int16_t displayScrollOffset = 0;
static int16_t displayMaxScroll = 0;
static unsigned long lastDisplayScroll = 0;
static unsigned long displayScrollEndTime = 0;

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
static void scrollDisplayText(const char* str);
static void writeRunToInfluxDB(time_t startEpoch, unsigned long durationMs,
                              IrrigationRunType runType);

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

  display.begin(0x3C, true);
  display.setRotation(1);
  Serial.println("OLED begun");

  displayOnWithTimeout();

  display.setTextSize(2);
  display.setTextWrap(false);
  display.setTextColor(SH110X_WHITE);
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
  char ipBuffer[64];
  snprintf(ipBuffer, sizeof(ipBuffer), "IP:\n%s",
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
    displayInfoOverride = false;
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

  // Display info override
  if (displayInfoOverride) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "IP:\n%s",
             WiFi.localIP().toString().c_str());
    printDisplay(buffer);
  } else if (sprinklerState == ON) {
    // Display sprinkler state
    char buffer[64];
    unsigned long duration = millis() - sprinklerStart;
    if (manualOverride != 0) {
      unsigned long remaining = manualOverride - millis();
      snprintf(buffer, sizeof(buffer), "Running:\n%.1f mins\nRemaining:\n%.1f mins",
               duration / 60000.0, remaining / 60000.0);
    } else {
      snprintf(buffer, sizeof(buffer), "Running:\n%.1f mins",
               duration / 60000.0);
    }
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

void buttonAPressed(bool longPress) {
  displayInfoOverride = false;
  currentRunType = MANUAL;
  sprinklerOnButtonHandler(longPress);
}

void buttonBPressed(bool longPress) {
  displayInfoOverride = false;
  sprinklerOffButtonHandler();
}

void buttonCPressed(bool longPress) {
  displayInfoOverride = longPress;
  displayOnWithTimeout();
}

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
  displayOnWithTimeout();
}

void sprinklerOffButtonHandler() {
  // Reset the override timeout and turn off the sprinkler
  manualOverride = 0;
  setSprinklerOff();
  displayOnWithTimeout();
}

void sprinklerOnWebHandler() {
  if (manualOverride != 0) {
    server.send(200, "application/json", "{\"status\":\"override\"}");
    return;
  }

  currentRunType = AUTOMATIC;
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
  lastRunStartEpoch = now();
  time_t t = myTZ.toLocal(lastRunStartEpoch);
  lastRunStartTime = t;

  snprintf(sprinklerStartDisplay, sizeof(sprinklerStartDisplay),
           "Start:\n%d-%02d-%02d %02d:%02d", year(t), month(t), day(t), hour(t),
           minute(t));
  pulseLatch(TRIGGER_PIN_ON);
}

static void setSprinklerOff() {
  printStringLn("turning sprinkler off");

  bool completedRun = sprinklerState == ON;
  unsigned long duration = 0;
  time_t startEpoch = 0;
  if (completedRun) {
    duration = millis() - sprinklerStart;
    startEpoch = lastRunStartEpoch;
    lastRunDurationMs = duration;
    lastRunType = currentRunType;
    sprinklerState = OFF;
    sprinklerStart = 0;

    snprintf(sprinklerDurationDisplay, sizeof(sprinklerDurationDisplay),
             "Duration:\n%.1f mins", duration / 60000.0);
  }

  // Always turn off to be safe
  pulseLatch(TRIGGER_PIN_OFF);

  // Record only runs that actually transitioned from ON to OFF.
  if (completedRun) {
    writeRunToInfluxDB(startEpoch, duration, lastRunType);
  }
}

static void writeRunToInfluxDB(time_t startEpoch, unsigned long durationMs,
                              IrrigationRunType runType) {
  HTTPClient http;
  http.setTimeout(2000);
  http.addHeader("Authorization", INFLUXDB_TOKEN);

  if (!http.begin(INFLUXDB_URL)) {
    printStringLn("InfluxDB request setup failed");
    return;
  }

  const char* runTypeTag = (runType == MANUAL) ? "manual" : "automatic";
  char lineProtocol[128];
  int lineLength;
  if (startEpoch >= 1609459200) {
    long long timestampMs = (long long)startEpoch * 1000LL;
    lineLength = snprintf(lineProtocol, sizeof(lineProtocol),
                          "irrigation_run,run_type=%s duration_ms=%lui %lld",
                          runTypeTag, durationMs, timestampMs);
  } else {
    lineLength = snprintf(lineProtocol, sizeof(lineProtocol),
                          "irrigation_run,run_type=%s duration_ms=%lui",
                          runTypeTag, durationMs);
  }

  int responseCode = http.POST((uint8_t*)lineProtocol, lineLength);
  if (responseCode >= 200 && responseCode < 300) {
    printStringLn("InfluxDB irrigation run recorded");
  } else {
    Serial.printf("InfluxDB write failed, HTTP status=%d\n", responseCode);
  }
  http.end();
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

  scrollDisplayText(str);

  display.display();
  display.setCursor(0, 0);
}

static void scrollDisplayText(const char* str) {
  const char* lineStart = str;
  int16_t y = 0;
  int16_t maxScroll = 0;

  while (lineStart != nullptr && y < display.height()) {
    const char* lineEnd = strchr(lineStart, '\n');
    size_t lineLength = lineEnd ? (size_t)(lineEnd - lineStart)
                                : strlen(lineStart);
    char line[64];
    if (lineLength >= sizeof(line)) {
      lineLength = sizeof(line) - 1;
    }
    memcpy(line, lineStart, lineLength);
    line[lineLength] = '\0';

    int16_t boundsX;
    int16_t boundsY;
    uint16_t textWidth;
    uint16_t textHeight;
    display.getTextBounds(line, 0, y, &boundsX, &boundsY, &textWidth,
                          &textHeight);
    if (textWidth > display.width() &&
        textWidth - display.width() > maxScroll) {
      maxScroll = textWidth - display.width();
    }

    if (!lineEnd) {
      lineStart = nullptr;
    } else {
      lineStart = lineEnd + 1;
    }
    y += 16;
  }

  const unsigned long nowMs = millis();
  if (maxScroll == 0) {
    displayScrollOffset = 0;
    displayScrollEndTime = 0;
  } else if (maxScroll != displayMaxScroll) {
    displayScrollOffset = min(displayScrollOffset, maxScroll);
    displayScrollEndTime = displayScrollOffset == maxScroll ? nowMs : 0;
  }
  displayMaxScroll = maxScroll;

  if (maxScroll > 0) {
    if (displayScrollOffset < maxScroll && nowMs - lastDisplayScroll >= 35) {
      ++displayScrollOffset;
      lastDisplayScroll = nowMs;
      if (displayScrollOffset == maxScroll) {
        displayScrollEndTime = nowMs;
      }
    } else if (displayScrollOffset == maxScroll &&
               nowMs - displayScrollEndTime >= 1000) {
      displayScrollOffset = 0;
      displayScrollEndTime = 0;
      lastDisplayScroll = nowMs;
    }
  }

  lineStart = str;
  y = 0;
  while (lineStart != nullptr && y < display.height()) {
    const char* lineEnd = strchr(lineStart, '\n');
    size_t lineLength = lineEnd ? (size_t)(lineEnd - lineStart)
                                : strlen(lineStart);
    char line[64];
    if (lineLength >= sizeof(line)) {
      lineLength = sizeof(line) - 1;
    }
    memcpy(line, lineStart, lineLength);
    line[lineLength] = '\0';

    int16_t boundsX;
    int16_t boundsY;
    uint16_t textWidth;
    uint16_t textHeight;
    display.getTextBounds(line, 0, y, &boundsX, &boundsY, &textWidth,
                          &textHeight);
    int16_t lineScroll = textWidth > display.width()
                             ? textWidth - display.width()
                             : 0;
    int16_t offset = min(displayScrollOffset, lineScroll);
    display.setCursor(-offset, y);
    display.print(line);

    if (!lineEnd) {
      lineStart = nullptr;
    } else {
      lineStart = lineEnd + 1;
    }
    y += 16;
  }
}

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
