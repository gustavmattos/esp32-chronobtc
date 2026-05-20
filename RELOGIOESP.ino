// === CYD ROLLING CLOCK ===
#include <Arduino.h>
#include <TimeLib.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Timezone.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

#define DHTPIN 22
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
float temperature = NAN;
long btcPrice = -1;
unsigned long lastBtcFetch = 0;
const unsigned long BTC_FETCH_INTERVAL = 60000;

#define TOUCH_CS

void Debug(String label, int val) {
  Serial.print(label);
  Serial.print("=");
  Serial.println(val);
}

static const char ntpServerName[] = "us.pool.ntp.org";
WiFiUDP Udp;
unsigned int localPort = 8888;
TimeChangeRule *tcr;

#include "secrets.h"

const bool SHOW_24HOUR = false;
const bool SHOW_AMPM = true;
const bool NOT_US_DATE = true;

TimeChangeRule myStandardTime = {"CST", First, Sun, Nov, 2, -8 * 60};
TimeChangeRule myDaylightSavingsTime = {"CDT", Second, Sun, Mar, 2, -3 * 60};
Timezone myTZ(myStandardTime, myDaylightSavingsTime);
static const int ntpSyncIntervalInSeconds = 300;

#include <TFT_eSPI.h>
#include <SPI.h>
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

int clockFont = 1;
int clockSize = 6;
int clockDatum = TL_DATUM;
uint16_t clockBackgroundColor = TFT_BLACK;
uint16_t clockFontColor = TFT_YELLOW;
int prevDay = 0;

void SetupCYD() {
  tft.init();
  tft.fillScreen(clockBackgroundColor);
  tft.setTextColor(clockFontColor, clockBackgroundColor);
  tft.setRotation(1);
  tft.setTextFont(clockFont);
  tft.setTextSize(clockSize);
  tft.setTextDatum(clockDatum);
  sprite.createSprite(tft.textWidth("8"), tft.fontHeight());
  sprite.setTextColor(clockFontColor, clockBackgroundColor);
  sprite.setRotation(1);
  sprite.setTextFont(clockFont);
  sprite.setTextSize(clockSize);
  sprite.setTextDatum(clockDatum);
}

#include "Digit.h"
Digit *digs[6];
int colons[2];
int timeY = 50;
int ampm[2];
bool ispm;

void CalculateDigitOffsets() {
  int y = timeY;
  int width = tft.width();
  int DigitWidth = tft.textWidth("8");
  int colonWidth = tft.textWidth(":");
  int left = SHOW_AMPM ? 10 : (width - DigitWidth * 6 - colonWidth * 2) / 2;
  digs[0]->SetXY(left, y);
  digs[1]->SetXY(digs[0]->X() + DigitWidth, y);
  colons[0] = digs[1]->X() + DigitWidth;
  digs[2]->SetXY(colons[0] + colonWidth, y);
  digs[3]->SetXY(digs[2]->X() + DigitWidth, y);
  colons[1] = digs[3]->X() + DigitWidth;
  digs[4]->SetXY(colons[1] + colonWidth, y);
  digs[5]->SetXY(digs[4]->X() + DigitWidth, y);
  ampm[0] = digs[5]->X() + DigitWidth + 4;
  ampm[1] = y - 2;
}

void SetupDigits() {
  tft.fillScreen(clockBackgroundColor);
  tft.setTextFont(clockFont);
  tft.setTextSize(clockSize);
  tft.setTextDatum(clockDatum);

  for (size_t i = 0; i < 6; i++) {
    digs[i] = new Digit(0);
    digs[i]->Height(tft.fontHeight());
  }
  CalculateDigitOffsets();
}

void DrawColons() {
  tft.setTextFont(clockFont);
  tft.setTextSize(clockSize);
  tft.setTextDatum(clockDatum);
  tft.drawChar(':', colons[0], timeY);
  tft.drawChar(':', colons[1], timeY);
}

void DrawAmPm() {
  if (SHOW_AMPM) {
    tft.setTextSize(3);
    tft.drawChar(ispm ? 'P' : 'A', ampm[0], ampm[1]);
    tft.drawChar('M', ampm[0], ampm[1] + tft.fontHeight());
  }
}

void DrawADigit(Digit *digg);
void DrawADigit(Digit *digg) {
  if (digg->Value() == digg->NewValue()) {
    sprite.drawNumber(digg->Value(), 0, 0);
    sprite.pushSprite(digg->X(), digg->Y());
  } else {
    for (size_t f = 0; f <= digg->Height(); f++) {
      digg->Frame(f);
      sprite.drawNumber(digg->Value(), 0, -digg->Frame());
      sprite.drawNumber(digg->NewValue(), 0, digg->Height() - digg->Frame());
      sprite.pushSprite(digg->X(), digg->Y());
      delay(5);
    }
    digg->Value(digg->NewValue());
  }
}

void DrawDigitsAtOnce() {
  tft.setTextDatum(TL_DATUM);
  for (size_t f = 0; f <= digs[0]->Height(); f++) {
    for (size_t di = 0; di < 6; di++) {
      Digit *dig = digs[di];
      if (dig->Value() == dig->NewValue()) {
        if (f == 0) {
          sprite.drawNumber(dig->Value(), 0, 0);
          sprite.pushSprite(dig->X(), dig->Y());
        }
      } else {
        dig->Frame(f);
        sprite.drawNumber(dig->Value(), 0, -dig->Frame());
        sprite.drawNumber(dig->NewValue(), 0, dig->Height() - dig->Frame());
        sprite.pushSprite(dig->X(), dig->Y());
      }
    }
    delay(5);
  }

  for (size_t di = 0; di < 6; di++) {
    digs[di]->Value(digs[di]->NewValue());
  }
}

void ParseDigits(time_t utc) {
  time_t local = myTZ.toLocal(utc, &tcr);
  digs[0]->NewValue((SHOW_24HOUR ? hour(local) : hourFormat12(local)) / 10);
  digs[1]->NewValue((SHOW_24HOUR ? hour(local) : hourFormat12(local)) % 10);
  digs[2]->NewValue(minute(local) / 10);
  digs[3]->NewValue(minute(local) % 10);
  digs[4]->NewValue(second(local) / 10);
  digs[5]->NewValue(second(local) % 10);
  ispm = isPM(local);
}

const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

void sendNTPpacket(IPAddress &address) {
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;
  packetBuffer[1] = 0;
  packetBuffer[2] = 6;
  packetBuffer[3] = 0xEC;
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  Udp.beginPacket(address, 123);
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

time_t getNtpTime() {
  IPAddress ntpServerIP;
  while (Udp.parsePacket() > 0) ;
  WiFi.hostByName(ntpServerName, ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);
      unsigned long secsSince1900;
      secsSince1900 = (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + 1;
    }
  }
  return timeNotSet;
}

void fetchBitcoinPrice() {
  if (millis() - lastBtcFetch < BTC_FETCH_INTERVAL) return;
  lastBtcFetch = millis();

  HTTPClient http;
  http.begin("https://api.binance.com/api/v3/ticker/price?symbol=BTCUSDT");
  if (http.GET() == 200) {
    StaticJsonDocument<128> doc;
    deserializeJson(doc, http.getString());
    btcPrice = doc["price"].as<float>();
  }
  http.end();
}

void DrawBitcoinPrice() {
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  int h = tft.fontHeight();
  tft.fillRect(0, 145 - h / 2, 320, h, TFT_BLACK);
  if (btcPrice > 0) {
    char buffer[24];
    sprintf(buffer, "BTC: $%ld", btcPrice);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(buffer, 320 / 2, 145);
    tft.setTextColor(clockFontColor, clockBackgroundColor);
  }
}

void DrawTemperature() {
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  int h = tft.fontHeight();
  tft.fillRect(0, 120 - h / 2, 320, h, TFT_BLACK);
  if (!isnan(temperature)) {
    char buffer[16];
    sprintf(buffer, "%.1f C", temperature);
    tft.drawString(buffer, 320 / 2, 120);
  }
}

void DrawDate(time_t utc) {
  time_t local = myTZ.toLocal(utc, &tcr);
  int dd = day(local);
  int mth = month(local);
  int yr = year(local);

  if (dd != prevDay) {
    tft.setTextDatum(BC_DATUM);
    char buffer[50];
    if (NOT_US_DATE) {
      sprintf(buffer, "%02d/%02d/%d", dd, mth, yr);
    } else {
      sprintf(buffer, "%02d/%02d/%d", mth, dd, yr);
    }
    tft.setTextSize(4);
    int h = tft.fontHeight();
    tft.fillRect(0, 210 - h, 320, h, TFT_BLACK);
    tft.drawString(buffer, 320 / 2, 210);

    int dow = weekday(local);
    String dayNames[] = {"", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    tft.fillRect(0, 170 - h, 320, h, TFT_BLACK);
    tft.drawString(dayNames[dow], 320 / 2, 170);
  }
}


void SetupWiFi() {
  while (WiFi.status() != WL_CONNECTED) {
    int numSSIDs = sizeof(credentials) / sizeof(credentials[0]);
    for (size_t i = 0; i < numSSIDs; i++) {
      String ssid = credentials[i][0];
      String pass = credentials[i][1];
      WiFi.begin(ssid.c_str(), pass.c_str());
      int tries = 10;
      while (WiFi.status() != WL_CONNECTED && tries-- > 0) {
        delay(500);
      }
      if (WiFi.status() == WL_CONNECTED) break;
    }
  }
}

void SetupNTP() {
  Udp.begin(localPort);
  setSyncProvider(getNtpTime);
  setSyncInterval(ntpSyncIntervalInSeconds);
  while (timeStatus() == timeNotSet) {
    delay(100);
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  SetupCYD();
  SetupWiFi();
  SetupNTP();
  SetupDigits();
}

time_t prevDisplay = 0;

void loop() {
  time_t current = now();
  if (current != prevDisplay) {
    prevDisplay = current;
    ParseDigits(prevDisplay);
    DrawDigitsAtOnce();
    DrawDate(prevDisplay);
    DrawColons();
    DrawAmPm();

    float temp = dht.readTemperature();
    if (!isnan(temp)) temperature = temp;
    DrawTemperature();

    fetchBitcoinPrice();
    DrawBitcoinPrice();
  }
  delay(100);
}
