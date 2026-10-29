#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <esp_heap_caps.h>
#include "secrets.h"
#include "trust_store.h"

TFT_eSPI tft;
TFT_eSprite pages[2] = {TFT_eSprite(&tft), TFT_eSprite(&tft)};
constexpr uint16_t BG=0, FG=1, MUTED=2, BLUE=3, GOLD=4, LINE=5, GREEN=6, ORANGE=7;
const uint16_t PALETTE[] = {TFT_BLACK, TFT_WHITE, 0x8410, TFT_CYAN, TFT_YELLOW, 0x2104, TFT_GREEN, 0xFD20};
struct Day { float low, high; int code, rain; char date[11]; };
struct Model {
  float temp=0, usd=0, btc=0; int code=0; bool night=false;
  bool weather=false, dollar=false, bitcoin=false, dailyDollar=false;
  uint32_t weatherAt=0, dollarAt=0, bitcoinAt=0;
  Day days[3]{};
};
Model data;
SemaphoreHandle_t dataLock;
int active=0;
uint32_t pageSince=0, lastPaint=0;
bool transitioning=false, displayReady=false;
bool otaReady=false;
volatile bool otaUpdating=false;
uint32_t transitionSince=0;

bool fetch(const char *url, JsonDocument &doc) {
  WiFiClientSecure client;
  client.setCACert(TRUST_STORE);
  client.setHandshakeTimeout(8);
  HTTPClient http;
  http.setConnectTimeout(5000); http.setTimeout(5000);
  if (!http.begin(client,url)) { Serial.printf("HTTP begin failed: %s\n",url); return false; }
  int status=http.GET();
  bool ok=false;
  if(status==200) {
    DeserializationError error=deserializeJson(doc,http.getString());
    ok=!error;
    if(error) Serial.printf("JSON error: %s (%s)\n",error.c_str(),url);
  } else {
    Serial.printf("HTTP status %d: %s (%s)\n",status,http.errorToString(status).c_str(),url);
  }
  http.end(); return ok;
}

void network(void *) {
  uint32_t weatherTry=0, marketTry=0;
  bool firstWeather=true, firstMarket=true;
  wl_status_t lastWifi=WL_IDLE_STATUS;
  uint32_t lastClockWarning=0;
  for (;;) {
    if(otaUpdating) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    wl_status_t wifi=WiFi.status();
    if(wifi!=lastWifi) {
      Serial.printf("Wi-Fi status: %d\n",wifi);
      if(wifi==WL_CONNECTED) Serial.printf("Wi-Fi connected, IP: %s, RSSI: %d dBm\n",WiFi.localIP().toString().c_str(),WiFi.RSSI());
      lastWifi=wifi;
    }
    if(wifi!=WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    // TLS certificate validation requires a valid system clock.
    if(time(nullptr)<1700000000) {
      if(millis()-lastClockWarning>=10000) { Serial.println("Waiting for NTP time"); lastClockWarning=millis(); }
      vTaskDelay(pdMS_TO_TICKS(1000)); continue;
    }
    uint32_t now=millis();
    if(firstWeather || now-weatherTry>=600000) {
      firstWeather=false; weatherTry=now;
      JsonDocument doc;
      if(fetch("https://api.open-meteo.com/v1/forecast?latitude=-27.5954&longitude=-48.5480&current=temperature_2m,weather_code,is_day&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max&timezone=America%2FSao_Paulo&forecast_days=4",doc)) {
        JsonObject current=doc["current"];
        JsonObject daily=doc["daily"];
        bool valid=current["temperature_2m"].is<float>() && current["weather_code"].is<int>() && daily["time"].size()==4;
        for(int i=1;i<4;i++) valid=valid && daily["temperature_2m_max"][i].is<float>() && daily["temperature_2m_min"][i].is<float>() && daily["weather_code"][i].is<int>() && daily["precipitation_probability_max"][i].is<int>();
        if(valid) {
          xSemaphoreTake(dataLock,portMAX_DELAY);
          data.temp=current["temperature_2m"]; data.code=current["weather_code"]; data.night=current["is_day"].as<int>()==0;
          for(int i=0;i<3;i++) {
            data.days[i].low=daily["temperature_2m_min"][i+1]; data.days[i].high=daily["temperature_2m_max"][i+1];
            data.days[i].code=daily["weather_code"][i+1]; data.days[i].rain=daily["precipitation_probability_max"][i+1];
            strlcpy(data.days[i].date,daily["time"][i+1].as<const char*>(),11);
          }
          data.weather=true; data.weatherAt=millis(); xSemaphoreGive(dataLock);
        }
      }
    }
    if(firstMarket || now-marketTry>=120000) {
      firstMarket=false; marketTry=now;
      JsonDocument doc;
      bool live=fetch("https://economia.awesomeapi.com.br/json/last/USD-BRL",doc);
      float price=live?doc["USDBRL"]["bid"].as<float>():0;
      live=live && isfinite(price) && price>0;
      if(!live) {
        doc.clear();
        if(fetch("https://api.frankfurter.dev/v2/rate/USD/BRL",doc)) price=doc["rate"].as<float>();
      }
      if(isfinite(price) && price>0) { xSemaphoreTake(dataLock,portMAX_DELAY); data.usd=price; data.dollar=true; data.dailyDollar=!live; data.dollarAt=millis(); xSemaphoreGive(dataLock); }
      doc.clear();
      // Coinbase returns BTC denominated in actual USD (rather than USDT).
      if(fetch("https://api.coinbase.com/v2/prices/BTC-USD/spot",doc)) {
        float price=doc["data"]["amount"].as<float>();
        if(isfinite(price) && price>0) { xSemaphoreTake(dataLock,portMAX_DELAY); data.btc=price; data.bitcoin=true; data.bitcoinAt=millis(); xSemaphoreGive(dataLock); }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void label(TFT_eSprite &s, const String &value,int x,int y,int font=2,uint16_t color=FG,int datum=TL_DATUM) {
  s.setTextDatum(datum); s.setTextColor(color,BG); s.drawString(value,x,y,font);
}
void pixelLabel(TFT_eSprite &s, const String &value,int x,int y,int size,uint16_t color) {
  s.setTextSize(size);
  label(s,value,x,y,1,color,TC_DATUM);
  s.setTextSize(1);
}
void icon(TFT_eSprite &s,int x,int y,int code,bool night=false) {
  if(code<=2) {
    if(night) {s.fillCircle(x+12,y+11,10,FG);s.fillCircle(x+17,y+7,9,BG);}
    else {
      s.fillCircle(x+12,y+12,7,GOLD);
      for(int a=0;a<8;a++) {
        float r=a*PI/4; s.drawLine(x+12+cos(r)*10,y+12+sin(r)*10,x+12+cos(r)*13,y+12+sin(r)*13,GOLD);
      }
    }
    if(code==0) return;
  }
  s.fillCircle(x+10,y+21,7,FG);s.fillCircle(x+20,y+16,10,FG);s.fillCircle(x+30,y+22,7,FG);s.fillRoundRect(x+7,y+21,25,8,4,FG);
  if(code==45 || code==48) {s.drawFastHLine(x+4,y+33,31,MUTED);s.drawFastHLine(x+9,y+37,25,MUTED);}
  else if((code>=71 && code<=77) || code==85 || code==86) {
    for(int i=0;i<3;i++) {s.drawLine(x+10+i*10,y+33,x+10+i*10,y+39,FG);s.drawLine(x+7+i*10,y+36,x+13+i*10,y+36,FG);}
  } else if(code>=95) {s.fillTriangle(x+20,y+29,x+13,y+39,x+21,y+36,GOLD);s.fillTriangle(x+21,y+35,x+16,y+44,x+27,y+33,GOLD);}
  else if(code>=51) for(int i=0;i<3;i++) {s.drawLine(x+12+i*9,y+33,x+9+i*9,y+39,BLUE);s.drawLine(x+13+i*9,y+33,x+10+i*9,y+39,BLUE);}
}
const char *condition(int code,bool night) {
  if(code==0) return night?"Ceu limpo":"Ensolarado";
  if(code<=2) return "Poucas nuvens";
  if(code==3) return "Nublado";
  if(code==45 || code==48) return "Nevoeiro";
  if(code>=95) return "Trovoadas";
  if((code>=71 && code<=77)||code==85||code==86) return "Neve";
  if(code>=80) return "Pancadas de chuva";
  if(code>=61) return "Chuva";
  return "Garoa";
}
String money(float value,bool real) {
  char b[30]; snprintf(b,sizeof(b),real?"%.2f":"%.0f",value);
  String v(b); int dot=v.indexOf('.'); if(dot<0)dot=v.length();
  for(int i=dot-3;i>0;i-=3)v= v.substring(0,i)+"."+v.substring(i);
  if(real) v.setCharAt(v.length()-3,',');
  return (real?"R$ ":"$ ")+v;
}
void paint(int index) {
  Model m; xSemaphoreTake(dataLock,portMAX_DELAY);m=data;xSemaphoreGive(dataLock);
  auto &s=pages[index]; s.fillSprite(BG);
  tm local{}; time_t epoch=time(nullptr); bool clockOk=epoch>1700000000;
  localtime_r(&epoch,&local);
  char hhmm[9]; if(clockOk)strftime(hhmm,sizeof(hhmm),"%H:%M:%S",&local);else strlcpy(hhmm,"--:--:--",9);
  const char *week[]={"DOM","SEG","TER","QUA","QUI","SEX","SAB"};
  if(index==0) {
    char date[40];snprintf(date,sizeof(date),"%s, %02d.%02d.%04d",week[local.tm_wday],local.tm_mday,local.tm_mon+1,local.tm_year+1900);
    pixelLabel(s,hhmm,160,26,6,GOLD);
    label(s,clockOk?date:"SINCRONIZANDO HORA",160,99,2,FG,TC_DATUM);
    pixelLabel(s,m.bitcoin?"BTC: "+money(m.btc,false):"BTC: --",160,145,2,GREEN);
    pixelLabel(s,m.dollar?(m.dailyDollar?"USD/BRL DIA: ":"USD/BRL: ")+money(m.usd,true):"USD/BRL: --",160,184,2,ORANGE);
  } else {
    label(s,"FLORIANOPOLIS",16,12,2,FG);label(s,String(hhmm).substring(0,5),304,12,2,GOLD,TR_DATUM);
    icon(s,27,47,m.weather?m.code:3,m.night);
    label(s,m.weather?String(m.temp,1)+" C":"--.- C",96,48,4,BLUE);
    label(s,m.weather?condition(m.code,m.night):"Buscando previsao...",160,91,2,FG,TC_DATUM);
    s.drawFastHLine(16,115,288,LINE);
    for(int i=0;i<3;i++) {
      int x=16+i*100;
      String day="--";
      if(m.weather) {tm d{};int yy,mm,dd;if(sscanf(m.days[i].date,"%d-%d-%d",&yy,&mm,&dd)==3) {d.tm_year=yy-1900;d.tm_mon=mm-1;d.tm_mday=dd;d.tm_hour=12;mktime(&d);day=week[d.tm_wday];}}
      label(s,day,x+44,121,2,GOLD,TC_DATUM);
      if(m.weather)icon(s,x+24,141,m.days[i].code);
      label(s,m.weather?String(lroundf(m.days[i].high))+" / "+String(lroundf(m.days[i].low)):"-- / --",x+44,182,2,FG,TC_DATUM);
      label(s,m.weather?String(m.days[i].rain)+"% chuva":"--% chuva",x+44,204,2,BLUE,TC_DATUM);
    }
  }
  bool stale=index==0?(!m.bitcoin || !m.dollar || millis()-m.bitcoinAt>600000 || millis()-m.dollarAt>600000):(!m.weather || millis()-m.weatherAt>1800000);
  label(s,WiFi.status()!=WL_CONNECTED?"SEM WI-FI":stale?"DADOS PENDENTES":"",16,225,1,MUTED);
  s.fillCircle(293,231,2,index==0?BLUE:LINE);s.fillCircle(303,231,2,index==1?BLUE:LINE);
}

void setup() {
  Serial.begin(115200);
  tft.init();tft.setRotation(1);tft.fillScreen(PALETTE[BG]);
  for(auto &s:pages) {s.setColorDepth(4);if(!s.createSprite(320,240)) {tft.setTextColor(TFT_WHITE,PALETTE[BG]);tft.drawString("Memoria insuficiente",12,100,2);return;}s.createPalette(PALETTE,8);}
  Serial.printf("Free heap: %u, largest block: %u\n",ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  dataLock=xSemaphoreCreateMutex();if(!dataLock)return;
  WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  configTzTime("BRT3","pool.ntp.org","time.cloudflare.com");
  xTaskCreatePinnedToCore(network,"network",12288,nullptr,1,nullptr,1);
  paint(0);pages[0].pushSprite(0,0);pageSince=millis();displayReady=true;
}
void loop() {
  if(!displayReady){delay(1000);return;}
  if(WiFi.status()==WL_CONNECTED) {
    if(!otaReady) {
      ArduinoOTA.setHostname("chronobtc");
      ArduinoOTA.setPassword(OTA_PASSWORD);
      ArduinoOTA.onStart([]() { otaUpdating=true; Serial.println("OTA update started"); });
      ArduinoOTA.onEnd([]() { Serial.println("OTA update complete"); });
      ArduinoOTA.onError([](ota_error_t error) { otaUpdating=false; Serial.printf("OTA error: %u\n",error); });
      ArduinoOTA.begin();
      otaReady=true;
      Serial.printf("OTA ready at %s:3232\n",WiFi.localIP().toString().c_str());
    }
    ArduinoOTA.handle();
  } else if(otaReady) {
    ArduinoOTA.end();
    otaReady=false;
  }
  uint32_t now=millis();
  if(transitioning) {
    float p=min(1.0f,(now-transitionSince)/350.0f);float eased=p*p*(3-2*p);
    int offset=lroundf(320*eased),next=1-active;
    // Opaque pages are clipped at the display edges; no blank frame.
    if(active==0){pages[active].pushSprite(-offset,0);pages[next].pushSprite(320-offset,0);}
    else {pages[active].pushSprite(offset,0);pages[next].pushSprite(offset-320,0);}
    if(p>=1) {active=next;transitioning=false;pageSince=millis();lastPaint=millis();}
    delay(1);return;
  }
  if(now-pageSince >= 10000U) {
    paint(1-active);transitioning=true;transitionSince=now;return;
  }
  if(now-lastPaint>=1000){paint(active);pages[active].pushSprite(0,0);lastPaint=now;}
  delay(5);
}
