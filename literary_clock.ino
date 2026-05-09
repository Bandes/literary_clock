// =============================================================================
// Literary Clock — Elecrow CrowPanel 4.2" ESP32-S3
// Uses Elecrow's native EPD driver files (copy into sketch folder):
//   EPD.cpp, EPD.h, EPD_Init.cpp, EPD_Init.h, EPDfont.h, spi.cpp, spi.h
// =============================================================================
// Data file format (LittleFS, /HH.txt, one line per minute):
//   Quote text|Author|Book Title
// =============================================================================

#include <Arduino.h>
#include "EPD.h"
uint8_t ImageBW[EPD_W * EPD_H / 8];  // 400x300/8 = 15000 bytes
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include "time.h"
#include "LittleFS.h"

// =============================================================================
// CONFIGURATION
// =============================================================================
// WiFi credentials are configured via WiFiManager captive portal on first boot.
// To reset credentials: hold BOOT button for 3 seconds at startup.
// TZ_STRING is used as fallback if IP geolocation fails.
const char* TZ_STRING  = "EST5EDT,M3.2.0,M11.1.0";   // fallback only, overridden by IP detection
const char* NTP_SERVER = "pool.ntp.org";

#define NTP_SYNC_INTERVAL_MIN  60  // re-sync NTP every 60 wakeups (~1 hr)

// GPIO for WiFiManager reset (hold at boot to clear saved credentials)
#define RESET_BUTTON_PIN  0   // BOOT button on most ESP32-S3 boards

// =============================================================================
// DISPLAY LAYOUT
// =============================================================================
#define DISPLAY_W  400
#define DISPLAY_H  300
#define MARGIN_X   8

#define ATTR_FONT     16
#define ATTR_CHAR_W   (ATTR_FONT / 2)
#define ATTR_LINE_H   18
#define TIME_FONT     24
#define TIME_CHAR_W   (TIME_FONT / 2)

// =============================================================================
// RTC MEMORY — persists through deep sleep
// =============================================================================
RTC_DATA_ATTR uint32_t totalMinutes   = 0;
RTC_DATA_ATTR int      lastHour       = -1;
RTC_DATA_ATTR int      lastMinute     = -1;
RTC_DATA_ATTR bool     timeEverSynced = false;

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================
bool   doWiFiTimeSync();
bool   getTime(struct tm* t);
void   deepSleepUntilNextMinute(int currentSec);
String getQuote(int hour, int minute);
void   parseLine(const String& raw, String& quote, String& author, String& book);
void   wordWrap(const String& text, std::vector<String>& lines, int charsPerRow);
int    countWrappedLines(const String& text, int charsPerRow);
void   renderDisplay(const String& quote, const String& author,
                     const String& book, int hour, int minute);
void   showMessage(const char* line1, const char* line2 = nullptr);
String detectTimezone();
String buildPOSIXtz(const String& ianaZone, long offsetSec);

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("\n=== WAKE ===");

  // Timezone MUST be set on every boot — does not survive deep sleep
  setenv("TZ", TZ_STRING, 1);
  tzset();

  totalMinutes++;
  Serial.printf("Boot #%lu\n", totalMinutes);

  // Check if BOOT button held — reset WiFi credentials
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    Serial.println("BOOT held — resetting WiFi credentials");
    WiFiManager wm;
    wm.resetSettings();
    Serial.println("Credentials cleared, restarting...");
    ESP.restart();
  }

  // Power on screen
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);
  delay(50);

  // Always do full EPD init
  Serial.println("EPD init...");
  EPD_GPIOInit();
  EPD_Init();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, 0, WHITE);
  Paint_Clear(WHITE);
  EPD_Clear_R26H(ImageBW);  // seed old-frame register with white
  EPD_Display(ImageBW);     // write white to current-frame register
  EPD_Update();             // one full refresh → clean white screen
  Serial.println("EPD ready");

  // NTP sync
  bool needSync = !timeEverSynced ||
                  (totalMinutes % NTP_SYNC_INTERVAL_MIN == 0);
  if (needSync) {
    Serial.println("Syncing NTP...");
    if (doWiFiTimeSync()) {
      timeEverSynced = true;
    } else {
      Serial.println("NTP sync failed");
    }
  }

  // Get time
  struct tm t;
  if (!getTime(&t)) {
    Serial.println("ERROR: no valid time");
    showMessage("No time sync.", "Check WiFi.");
    EPD_Display(ImageBW);
    EPD_Update();
    EPD_Sleep();
    deepSleepUntilNextMinute(0);
  }

  Serial.printf("Time: %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);

  // Skip if same minute
  if (t.tm_hour == lastHour && t.tm_min == lastMinute) {
    Serial.println("Same minute, skipping");
    EPD_Sleep();
    deepSleepUntilNextMinute(t.tm_sec);
  }

  lastHour   = t.tm_hour;
  lastMinute = t.tm_min;

  // Load quote
  if (!LittleFS.begin(false)) {
    Serial.println("ERROR: LittleFS failed");
    showMessage("Storage error.", "Re-upload data.");
    EPD_Display(ImageBW);
    EPD_Update();
    EPD_Sleep();
    deepSleepUntilNextMinute(t.tm_sec);
  }
  Serial.println("LittleFS OK");

  String raw = getQuote(t.tm_hour, t.tm_min);
  String quote, author, book;
  parseLine(raw, quote, author, book);
  Serial.printf("Raw:    %s\n", raw.c_str());
  Serial.printf("Quote:  %s\n", quote.c_str());
  Serial.printf("Author: %s\n", author.c_str());
  Serial.printf("Book:   %s\n", book.c_str());

  // Render and push to display
  Serial.println("Rendering...");
  renderDisplay(quote, author, book, t.tm_hour, t.tm_min);
  Serial.println("Pushing to display...");
  EPD_Display(ImageBW);
  EPD_Update();
  EPD_Sleep();
  Serial.println("Display updated");

  deepSleepUntilNextMinute(t.tm_sec);
}

void loop() {}

// =============================================================================
// WIFI & NTP
// =============================================================================
bool doWiFiTimeSync() {
  // WiFiManager handles credentials — shows captive portal on first boot
  // or if credentials have been reset. Subsequent boots connect automatically.
  WiFiManager wm;
  wm.setConnectTimeout(30);   // seconds to try connecting before portal
  wm.setTimeout(180);         // seconds portal stays open before giving up

  // Portal callback removed — lambda with EPD calls caused boot crash on ESP32-S3

  if (!wm.autoConnect("LiteraryClock")) {
    Serial.println("WiFiManager failed / timed out");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  Serial.println("WiFi connected");

  // Detect timezone from IP geolocation
  String tz = detectTimezone();
  if (tz.length() > 0) {
    Serial.printf("Detected TZ: %s\n", tz.c_str());
    setenv("TZ", tz.c_str(), 1);
  } else {
    Serial.println("TZ detection failed, using fallback");
    setenv("TZ", TZ_STRING, 1);
  }
  tzset();

  // Sync NTP
  configTime(0, 0, NTP_SERVER);

  struct tm t;
  for (int i = 0; i < 20; i++) {
    if (getLocalTime(&t) && t.tm_year > 100) {
      Serial.printf("NTP OK: %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      return true;
    }
    delay(500);
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return false;
}

// =============================================================================
// TIMEZONE DETECTION
// =============================================================================
String detectTimezone() {
  HTTPClient http;
  http.begin("http://ip-api.com/json/?fields=timezone,offset");
  http.setTimeout(5000);
  int code = http.GET();
  Serial.printf("TZ API response code: %d\n", code);
  Serial.printf("TZ API error: %s\n", http.errorToString(code).c_str());
  if (code != 200) {
    Serial.printf("TZ API failed, code %d\n", code);
    http.end();
    return "";
  }

  String payload = http.getString();
  http.end();
  Serial.printf("TZ payload: %s\n", payload.c_str());

  // Parse timezone field from JSON
  // e.g. {"timezone":"America/New_York","offset":-14400}
  int tzStart = payload.indexOf("\"timezone\":\"");
  if (tzStart < 0) return "";
  tzStart += 12;
  int tzEnd = payload.indexOf("\"", tzStart);
  if (tzEnd < 0) return "";
  String ianaZone = payload.substring(tzStart, tzEnd);

  // Parse UTC offset in seconds
  int offStart = payload.indexOf("\"offset\":");
  if (offStart < 0) return "";
  offStart += 9;
  int offEnd = payload.indexOf("}", offStart);
  long offsetSec = payload.substring(offStart, offEnd).toInt();

  return buildPOSIXtz(ianaZone, offsetSec);
}

String buildPOSIXtz(const String& ianaZone, long offsetSec) {
  // Known IANA → POSIX mappings with correct DST rules
  if (ianaZone == "America/New_York")      return "EST5EDT,M3.2.0,M11.1.0";
  if (ianaZone == "America/Chicago")       return "CST6CDT,M3.2.0,M11.1.0";
  if (ianaZone == "America/Denver")        return "MST7MDT,M3.2.0,M11.1.0";
  if (ianaZone == "America/Phoenix")       return "MST7";
  if (ianaZone == "America/Los_Angeles")   return "PST8PDT,M3.2.0,M11.1.0";
  if (ianaZone == "America/Anchorage")     return "AKST9AKDT,M3.2.0,M11.1.0";
  if (ianaZone == "Pacific/Honolulu")      return "HST10";
  if (ianaZone == "America/Halifax")       return "AST4ADT,M3.2.0,M11.1.0";
  if (ianaZone == "America/St_Johns")      return "NST3:30NDT,M3.2.0,M11.1.0";
  if (ianaZone == "Europe/London")         return "GMT0BST,M3.5.0/1,M10.5.0";
  if (ianaZone == "Europe/Paris")          return "CET-1CEST,M3.5.0,M10.5.0/3";
  if (ianaZone == "Europe/Berlin")         return "CET-1CEST,M3.5.0,M10.5.0/3";
  if (ianaZone == "Europe/Amsterdam")      return "CET-1CEST,M3.5.0,M10.5.0/3";
  if (ianaZone == "Europe/Stockholm")      return "CET-1CEST,M3.5.0,M10.5.0/3";
  if (ianaZone == "Europe/Helsinki")       return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (ianaZone == "Europe/Athens")         return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (ianaZone == "Australia/Sydney")      return "AEST-10AEDT,M10.1.0,M4.1.0/3";
  if (ianaZone == "Australia/Melbourne")   return "AEST-10AEDT,M10.1.0,M4.1.0/3";
  if (ianaZone == "Australia/Brisbane")    return "AEST-10";
  if (ianaZone == "Australia/Adelaide")    return "ACST-9:30ACDT,M10.1.0,M4.1.0/3";
  if (ianaZone == "Australia/Perth")       return "AWST-8";
  if (ianaZone == "Asia/Tokyo")            return "JST-9";
  if (ianaZone == "Asia/Shanghai")         return "CST-8";
  if (ianaZone == "Asia/Hong_Kong")        return "HKT-8";
  if (ianaZone == "Asia/Singapore")        return "SGT-8";
  if (ianaZone == "Asia/Kolkata")          return "IST-5:30";
  if (ianaZone == "Asia/Dubai")            return "GST-4";
  if (ianaZone == "Asia/Seoul")            return "KST-9";
  if (ianaZone == "Pacific/Auckland")      return "NZST-12NZDT,M9.5.0,M4.1.0/3";

  // Fallback: build fixed-offset string from UTC offset (no DST)
  long hours = -offsetSec / 3600;
  long mins  = abs((offsetSec % 3600) / 60);
  char buf[16];
  if (mins == 0)
    snprintf(buf, sizeof(buf), "UTC%ld", hours);
  else
    snprintf(buf, sizeof(buf), "UTC%ld:%02ld", hours, mins);
  Serial.printf("Unknown zone %s, using fixed offset %s\n",
                ianaZone.c_str(), buf);
  return String(buf);
}

bool getTime(struct tm* t) {
  return getLocalTime(t) && t->tm_year > 100;
}

void deepSleepUntilNextMinute(int currentSec) {
  int secs = 60 - currentSec;
  if (secs < 1) secs = 1;
  Serial.printf("Sleeping %ds\n", secs);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
  esp_deep_sleep_start();
}

// =============================================================================
// QUOTE LOOKUP
// =============================================================================
String getQuote(int hour, int minute) {
  String filename = "/" + String(hour) + ".txt";
  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.printf("File not found: %s\n", filename.c_str());
    return "Time is a river without banks.|Marc Chagall|";
  }
  String line;
  int n = 0;
  while (file.available()) {
    line = file.readStringUntil('\n');
    line.trim();
    // Clean up HTML line breaks from the JohannesNE dataset
    line.replace("<br />", " ");
    line.replace("<br/>", " ");
    line.replace("<br>", " ");
    // Collapse double spaces that result from replacements
    while (line.indexOf("  ") >= 0) line.replace("  ", " ");
    if (n == minute) {
      file.close();
      return line;
    }
    n++;
  }
  file.close();
  return "All we have to decide is what to do with the time given us.|Tolkien|Fellowship of the Ring";
}

// =============================================================================
// PARSE PIPE-DELIMITED LINE
// =============================================================================
void parseLine(const String& raw, String& quote, String& author, String& book) {
  int p1 = raw.indexOf('|');
  int p2 = (p1 >= 0) ? raw.indexOf('|', p1 + 1) : -1;

  if (p1 < 0) {
    quote = raw; author = ""; book = "";
  } else if (p2 < 0) {
    quote  = raw.substring(0, p1);
    author = raw.substring(p1 + 1);
    book   = "";
  } else {
    quote  = raw.substring(0, p1);
    author = raw.substring(p1 + 1, p2);
    book   = raw.substring(p2 + 1);
  }
  quote.trim(); author.trim(); book.trim();
}

// =============================================================================
// WORD WRAP
// =============================================================================
void wordWrap(const String& text, std::vector<String>& lines, int charsPerRow) {
  String rem = text;
  rem.trim();
  while (rem.length() > 0) {
    if ((int)rem.length() <= charsPerRow) {
      lines.push_back(rem);
      break;
    }
    int brk = charsPerRow;
    while (brk > 0 && rem[brk] != ' ') brk--;
    if (brk == 0) brk = charsPerRow;
    lines.push_back(rem.substring(0, brk));
    rem = rem.substring(brk);
    rem.trim();
  }
}

int countWrappedLines(const String& text, int charsPerRow) {
  std::vector<String> lines;
  wordWrap(text, lines, charsPerRow);
  return lines.size();
}

// =============================================================================
// RENDER
// =============================================================================

// Available font sizes (largest first for scaling)
static const int fontSizes[] = {48, 32, 24, 16, 12};
static const int numFontSizes = 5;

void renderDisplay(const String& quote, const String& author,
                   const String& book, int hour, int minute) {
  Paint_Clear(WHITE);

  // Reserve space: time row at top, attribution at bottom
  int timeRowH = TIME_FONT + 4;
  int attrRowH = ATTR_FONT + 4;
  int quoteAreaH = DISPLAY_H - timeRowH - attrRowH - MARGIN_X;

  // Pick largest font where quote fits in available area
  int fontSz = fontSizes[numFontSizes - 1];  // smallest as fallback
  for (int i = 0; i < numFontSizes; i++) {
    int sz = fontSizes[i];
    int charW = sz / 2;
    int charsPerRow = (DISPLAY_W - MARGIN_X * 2) / charW;
    int lineH = sz + 2;
    int nLines = countWrappedLines(quote, charsPerRow);
    if (nLines * lineH <= quoteAreaH) {
      fontSz = sz;
      break;
    }
  }

  int charW = fontSz / 2;
  int charsPerRow = (DISPLAY_W - MARGIN_X * 2) / charW;
  int lineH = fontSz + 2;

  Serial.printf("Font: %d, chars/row: %d\n", fontSz, charsPerRow);

  // Time — top right (always TIME_FONT)
  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hour, minute);
  int timeX = DISPLAY_W - (strlen(timeBuf) * TIME_CHAR_W) - MARGIN_X;
  EPD_ShowString(timeX, MARGIN_X, timeBuf, TIME_FONT, BLACK);

  // Quote — below time, vertically centered in available area
  std::vector<String> qLines;
  wordWrap(quote, qLines, charsPerRow);
  int totalTextH = qLines.size() * lineH;
  int quoteY0 = timeRowH + (quoteAreaH - totalTextH) / 2;
  if (quoteY0 < timeRowH) quoteY0 = timeRowH;

  int y = quoteY0;
  for (const auto& ln : qLines) {
    if (y + fontSz > DISPLAY_H - attrRowH) break;
    EPD_ShowString(MARGIN_X, y, ln.c_str(), fontSz, BLACK);
    y += lineH;
  }

  // Attribution — bottom (always ATTR_FONT)
  String attr = "";
  if (author.length() > 0 && book.length() > 0)
    attr = "- " + author + ", " + book;
  else if (author.length() > 0)
    attr = "- " + author;
  else if (book.length() > 0)
    attr = book;

  if (attr.length() > 0) {
    int attrChars = (DISPLAY_W - MARGIN_X * 2) / ATTR_CHAR_W;
    if ((int)attr.length() > attrChars)
      attr = attr.substring(0, attrChars - 1);
    EPD_ShowString(MARGIN_X, DISPLAY_H - ATTR_FONT - 2,
                   attr.c_str(), ATTR_FONT, BLACK);
  }
}

// =============================================================================
// ERROR SCREEN
// =============================================================================
void showMessage(const char* line1, const char* line2) {
  Paint_Clear(WHITE);
  EPD_ShowString(MARGIN_X, 20, line1, 24, BLACK);
  if (line2) EPD_ShowString(MARGIN_X, 48, line2, 24, BLACK);
}
