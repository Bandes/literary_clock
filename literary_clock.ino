// =============================================================================
// Literary Clock — Elecrow CrowPanel 4.2" ESP32-S3
// =============================================================================
// Data file format (LittleFS, /HH.txt, one or more lines per minute):
//   MM|Quote text|Author|Book Title
// Multiple lines may share the same MM; one is chosen at random per wake.
// =============================================================================

#include <Arduino.h>
#include <SPI.h>
#include "EPD.h"
uint8_t ImageBW[EPD_W * EPD_H / 8];  // 400x300/8 = 15000 bytes
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include "time.h"
#include "LittleFS.h"
#include "esp_random.h"
#include <cstring>
#include <vector>

#include <Adafruit_GFX.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerif18pt7b.h>
#include <Fonts/FreeSerif24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include "EPDDisplay.h"

EPDDisplay gfx(EPD_W, EPD_H);

// =============================================================================
// CONFIGURATION
// =============================================================================
// WiFi credentials are configured via WiFiManager captive portal on first boot.
// To reset credentials: hold BOOT button for 3 seconds at startup.
// TZ_STRING is used as fallback if IP geolocation fails.
const char* TZ_STRING  = "EST5EDT,M3.2.0,M11.1.0";   // fallback only, overridden by IP detection
const char* NTP_SERVERS[] = {"pool.ntp.org", "time.google.com", "time.nist.gov"};
#define NUM_NTP_SERVERS (sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]))

#define WIFI_CONNECT_TIMEOUT_S  10  // fast connect — no captive portal on wake
#define NTP_POLL_ATTEMPTS      3    // SNTP poll attempts per wake
#define NTP_SYNC_INTERVAL_S   (15 * 60)  // force a resync at least this often, regardless of wake cadence

// GPIO for WiFiManager reset (hold at boot to clear saved credentials)
#define RESET_BUTTON_PIN  0   // BOOT button on most ESP32-S3 boards

// =============================================================================
// DISPLAY LAYOUT
// =============================================================================
#define DISPLAY_W  EPD_W
#define DISPLAY_H  EPD_H
#define MARGIN_X   8

// =============================================================================
// RTC MEMORY — persists through deep sleep
// =============================================================================
RTC_DATA_ATTR uint32_t totalMinutes   = 0;
RTC_DATA_ATTR int      lastHour       = -1;
RTC_DATA_ATTR int      lastMinute     = -1;
RTC_DATA_ATTR bool     timeEverSynced = false;
RTC_DATA_ATTR char     cachedTZ[48]   = "";  // detected POSIX TZ, persists across deep sleep
RTC_DATA_ATTR time_t   lastSyncEpoch  = 0;   // time() at last successful SNTP sync

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================
bool   connectWiFi();
bool   fastSNTPSync(struct tm* t, bool forceFullSync = false);
bool   getTime(struct tm* t);
void   deepSleepUntilNextMinute(int currentSec);
String getQuote(int hour, int minute);
void   parseLine(const String& raw, String& quote, String& author, String& book);
void   wordWrapPixel(const GFXfont* font, const String& text, int maxWidth,
                     std::vector<String>& lines);
void   renderDisplay(const String& quote, const String& author,
                     const String& book, int hour, int minute);
void   showMessage(const char* line1, const char* line2 = nullptr);
String detectTimezone();
String buildPOSIXtz(const String& ianaZone, long offsetSec);
void   sanitizeASCII(String& s);

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("\n=== WAKE ===");

  // TZ env var MUST be set on every boot — it does not survive deep sleep.
  // Use the last-detected zone if we have one, so non-sync wakeups don't
  // fall back to the hardcoded default and skew local time.
  setenv("TZ", cachedTZ[0] ? cachedTZ : TZ_STRING, 1);
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

  // Read the ESP32 internal RTC — it keeps running through deep sleep, so
  // this is the real current time (drift-corrected at each NTP sync), not a
  // wakeup counter that assumes exactly 60s per cycle.
  struct tm t;
  bool haveTime = getTime(&t);
  bool syncDue = (time(nullptr) - lastSyncEpoch) >= NTP_SYNC_INTERVAL_S;
  if (haveTime) {
    Serial.printf("RTC time: %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
    // Same minute as last render (woke early) — bail out before EPD/WiFi,
    // unless we haven't synced in a while: a resync must not depend on
    // how often the device happens to wake.
    if (t.tm_hour == lastHour && t.tm_min == lastMinute && !syncDue) {
      Serial.println("Same minute, skipping");
      deepSleepUntilNextMinute(t.tm_sec);
    }
  }

  // Power on screen
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);
  delay(50);

  // Always do full EPD init
  Serial.println("EPD init...");
  EPD_GPIOInit();
  EPD_Init();
  // Seed old-frame register (0x26) with white — no refresh yet
  EPD_WriteWhiteToOldFrame();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, 0, WHITE);
  Paint_Clear(WHITE);
  Serial.println("EPD ready");

  // SNTP sync on every wake to correct RC drift. Only use WiFiManager when no
  // credentials are saved yet or when explicitly reset via BOOT button (above).
  bool connected = connectWiFi();
  if (connected) {
    Serial.println("Syncing SNTP...");
    // Force full resync every NTP_SYNC_INTERVAL_S to combat drift
    if (fastSNTPSync(&t, syncDue)) {
      timeEverSynced = true;
      lastSyncEpoch = time(nullptr);
      Serial.printf("SNTP synced: %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
    } else {
      Serial.println("SNTP sync failed, using RTC");
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  } else {
    Serial.println("WiFi connect failed, using RTC");
  }

  // Re-read the clock after any sync to get the freshest, corrected time.
  if (!getTime(&t)) {
    Serial.println("ERROR: no valid time");
    showMessage("No time sync.", "Check WiFi.");
    EPD_Display(ImageBW);
    EPD_Update();
    EPD_Sleep();
    deepSleepUntilNextMinute(0);
  }
  Serial.printf("Local time: %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);

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
  // Sanitize any remaining non-ASCII (e.g. smart quotes surviving LittleFS)
  sanitizeASCII(raw);
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
bool connectWiFi() {
  WiFi.mode(WIFI_STA);

  // Stored credentials from a previous WiFiManager run — fast reconnect,
  // no portal, so most wakeups don't pay a captive-portal timeout.
  if (WiFi.SSID().length() > 0) {
    WiFi.begin();
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < WIFI_CONNECT_TIMEOUT_S * 1000UL) {
      delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected (fast)");
      return true;
    }
    Serial.println("Fast WiFi connect timed out, trying portal");
  }

  // No stored credentials, or fast connect failed — fall back to the
  // WiFiManager captive portal.
  WiFiManager wm;
  wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_S);
  wm.setTimeout(180);  // seconds portal stays open before giving up
  if (!wm.autoConnect("LiteraryClock")) {
    Serial.println("WiFiManager failed / timed out");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  Serial.println("WiFi connected (portal)");
  return true;
}

bool fastSNTPSync(struct tm* t, bool forceFullSync) {
  // Detect timezone once from IP geolocation and cache it across deep sleep;
  // every later wake reuses cachedTZ instead of re-querying.
  if (!cachedTZ[0]) {
    String tz = detectTimezone();
    if (tz.length() == 0) tz = TZ_STRING;
    strlcpy(cachedTZ, tz.c_str(), sizeof(cachedTZ));
  }
  setenv("TZ", cachedTZ, 1);
  tzset();

  // configTzTime() with the POSIX TZ string handles DST correctly and
  // re-anchors the internal RTC on success, correcting RC-oscillator drift.
  configTzTime(cachedTZ, NTP_SERVERS[0], NTP_SERVERS[1], NTP_SERVERS[2]);

  // Force a fresh sync if requested: set time to 0 to invalidate cache
  if (forceFullSync) {
    Serial.println("Forcing full NTP resync...");
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    // Give SNTP a moment to queue the request
    delay(100);
  }

  // Aggressive polling for sync with longer timeout on forced syncs
  int attempts = forceFullSync ? 5 : NTP_POLL_ATTEMPTS;
  unsigned long timeout = forceFullSync ? 5000UL : 2000UL;
  
  for (int i = 0; i < attempts; i++) {
    if (getLocalTime(t, timeout) && t->tm_year > 100) {
      return true;
    }
    delay(100);  // Small gap between attempts
  }
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

  // Collect every quote tagged with this minute (format: MM|Quote|Author|Book).
  std::vector<String> cands;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int bar = line.indexOf('|');
    if (bar < 0) continue;
    if (line.substring(0, bar).toInt() == minute) {
      cands.push_back(line.substring(bar + 1));
    }
  }
  file.close();

  if (cands.empty()) {
    // A handful of minutes have no quote in the dataset — graceful fallback.
    return "All we have to decide is what to do with the time given us.|Tolkien|Fellowship of the Ring";
  }

  // Pick one at random so repeated minutes vary day to day.
  String chosen = cands[esp_random() % cands.size()];
  Serial.printf("Minute %d: %d candidate(s)\n", minute, (int)cands.size());

  // Data is pre-sanitized at build time; this is just a safety net.
  chosen.replace("<br />", " ");
  chosen.replace("<br/>", " ");
  chosen.replace("<br>", " ");
  while (chosen.indexOf("  ") >= 0) chosen.replace("  ", " ");
  return chosen;
}

// =============================================================================
// SANITIZE — replace common UTF-8 sequences with ASCII at runtime
// =============================================================================
void sanitizeASCII(String& s) {
  // Smart quotes
  s.replace("\xE2\x80\x98", "'");   // left single quote
  s.replace("\xE2\x80\x99", "'");   // right single quote / apostrophe
  s.replace("\xE2\x80\x9C", "\"");  // left double quote
  s.replace("\xE2\x80\x9D", "\"");  // right double quote
  // Dashes
  s.replace("\xE2\x80\x93", "-");   // en dash
  s.replace("\xE2\x80\x94", "--");  // em dash
  s.replace("\xE2\x80\x91", "-");   // non-breaking hyphen
  // Ellipsis
  s.replace("\xE2\x80\xA6", "...");
  // Non-breaking space
  s.replace("\xC2\xA0", " ");
  // Strip any remaining non-ASCII bytes
  String clean;
  clean.reserve(s.length());
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if ((uint8_t)c >= 0x80) continue;  // skip non-ASCII bytes
    clean += c;
  }
  s = clean;
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
// WORD WRAP (pixel-based for proportional fonts)
// =============================================================================

// Measure cursor advance width of a string in given font
int measureTextWidth(const GFXfont* font, const char* text) {
  int w = 0;
  while (*text) {
    uint8_t c = (uint8_t)*text;
    if (c < font->first || c > font->last) { text++; continue; }
    GFXglyph* glyph = &font->glyph[c - font->first];
    w += glyph->xAdvance;
    text++;
  }
  return w;
}

void wordWrapPixel(const GFXfont* font, const String& text, int maxWidth,
                   std::vector<String>& lines) {
  String rem = text;
  rem.trim();
  String currentLine = "";

  while (rem.length() > 0) {
    int spaceIdx = rem.indexOf(' ');
    String word;
    if (spaceIdx < 0) {
      word = rem;
      rem = "";
    } else {
      word = rem.substring(0, spaceIdx);
      rem = rem.substring(spaceIdx + 1);
      rem.trim();
    }

    String testLine = currentLine.length() > 0 ? currentLine + " " + word : word;
    int w = measureTextWidth(font, testLine.c_str());

    if (w <= maxWidth || currentLine.length() == 0) {
      currentLine = testLine;
    } else {
      lines.push_back(currentLine);
      currentLine = word;
    }
  }
  if (currentLine.length() > 0) {
    lines.push_back(currentLine);
  }
}

// =============================================================================
// RENDER
// =============================================================================

// Quote fonts: FreeSerif, largest to smallest
static const GFXfont* quoteFonts[] = {
  &FreeSerif24pt7b,
  &FreeSerif18pt7b,
  &FreeSerif12pt7b,
  &FreeSerif9pt7b,
};
static const int numQuoteFonts = 4;

void renderDisplay(const String& quote, const String& author,
                   const String& book, int hour, int minute) {
  Paint_Clear(WHITE);

  int textWidth = DISPLAY_W - MARGIN_X * 2;

  // Time — top right (FreeSans 9pt)
  const GFXfont* timeFont = &FreeSans9pt7b;
  gfx.setFont(timeFont);
  gfx.setTextColor(BLACK);
  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hour, minute);
  int16_t tx1, ty1;
  uint16_t tw, th;
  gfx.getTextBounds(timeBuf, 0, 0, &tx1, &ty1, &tw, &th);
  int timeLineH = timeFont->yAdvance;
  gfx.setCursor(DISPLAY_W - tw - MARGIN_X, MARGIN_X + th);
  gfx.print(timeBuf);

  // Attribution string
  const GFXfont* attrFont = &FreeSans9pt7b;
  String attr = "";
  if (author.length() > 0 && book.length() > 0)
    attr = "- " + author + ", " + book;
  else if (author.length() > 0)
    attr = "- " + author;
  else if (book.length() > 0)
    attr = book;
  int attrLineH = attrFont->yAdvance;

  // Available area for quote
  int quoteTop = MARGIN_X + timeLineH + 4;
  int quoteBot = DISPLAY_H - (attr.length() > 0 ? attrLineH + 6 : MARGIN_X);
  int quoteAreaH = quoteBot - quoteTop;

  // Pick largest font where quote fits
  const GFXfont* chosenFont = quoteFonts[numQuoteFonts - 1];
  std::vector<String> qLines;
  int lineH = chosenFont->yAdvance;

  Serial.printf("Quote area: top=%d bot=%d h=%d\n", quoteTop, quoteBot, quoteAreaH);
  for (int i = 0; i < numQuoteFonts; i++) {
    const GFXfont* f = quoteFonts[i];
    std::vector<String> testLines;
    wordWrapPixel(f, quote, textWidth, testLines);
    int testLineH = f->yAdvance;
    int totalH = testLines.size() * testLineH;
    Serial.printf("  Font[%d] yAdv=%d lines=%d totalH=%d %s\n",
                  i, testLineH, (int)testLines.size(), totalH,
                  totalH <= quoteAreaH ? "FITS" : "no");
    if (totalH <= quoteAreaH) {
      chosenFont = f;
      qLines = testLines;
      lineH = testLineH;
      break;
    }
    if (i == numQuoteFonts - 1) {
      qLines = testLines;
      lineH = testLineH;
    }
  }

  Serial.printf("Chosen font yAdvance: %d, lines: %d\n", lineH, (int)qLines.size());

  // Draw quote — vertically centered in available area
  int totalTextH = qLines.size() * lineH;
  // If text overflows even at smallest font, just start at the top
  int quoteY;
  if (totalTextH >= quoteAreaH) {
    quoteY = quoteTop + lineH;
  } else {
    quoteY = quoteTop + (quoteAreaH - totalTextH) / 2 + lineH;
  }

  gfx.setFont(chosenFont);
  gfx.setTextColor(BLACK);
  for (const auto& ln : qLines) {
    if (quoteY > quoteBot) break;  // don't overwrite attribution
    gfx.setCursor(MARGIN_X, quoteY);
    gfx.print(ln);
    quoteY += lineH;
  }

  // Attribution — bottom
  if (attr.length() > 0) {
    gfx.setFont(attrFont);
    gfx.setTextColor(BLACK);
    // Truncate if too wide
    int16_t ax1, ay1;
    uint16_t aw, ah;
    gfx.getTextBounds(attr.c_str(), 0, 0, &ax1, &ay1, &aw, &ah);
    while ((int)aw > textWidth && attr.length() > 3) {
      attr = attr.substring(0, attr.length() - 1);
      gfx.getTextBounds(attr.c_str(), 0, 0, &ax1, &ay1, &aw, &ah);
    }
    gfx.setCursor(MARGIN_X, DISPLAY_H - MARGIN_X);
    gfx.print(attr);
  }
}

// =============================================================================
// ERROR SCREEN
// =============================================================================
void showMessage(const char* line1, const char* line2) {
  Paint_Clear(WHITE);
  gfx.setFont(&FreeSans9pt7b);
  gfx.setTextColor(BLACK);
  gfx.setCursor(MARGIN_X, 30);
  gfx.print(line1);
  if (line2) {
    gfx.setCursor(MARGIN_X, 55);
    gfx.print(line2);
  }
}
