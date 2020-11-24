#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <RTClib.h> //adafruit
#include <TM1637Display.h> //orpaz
#include <NTP.h> //github.com/sstaub/NTP
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
#include <DHTesp.h>
#include <EasyButton.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>

// RTC
RTC_DS3231 rtc;

// Display 
#define CLK 0
#define DIO 2
TM1637Display display(CLK, DIO);
const uint8_t SEG_BOOT[] = {
    SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, // b
    SEG_C | SEG_D | SEG_E | SEG_G,         // o
    SEG_C | SEG_D | SEG_E | SEG_G,         // o
    SEG_D | SEG_E | SEG_F | SEG_G          // t
};
const uint8_t SEG_CEL[] = {
    SEG_A | SEG_D | SEG_E | SEG_F, // C
    0x00, 0x00, 0x00
};
const uint8_t SEG_HUM[] = {
    SEG_B | SEG_C | SEG_E | SEG_F | SEG_G, // H
    0x00, 0x00, 0x00
};

// WiFi Static
IPAddress ip_static(192,168,22,55);
IPAddress subnet(255,255,255,0);
IPAddress gateway(192,168,22,2);
IPAddress dns1(192,168,22,6);
IPAddress dns2(8,8,4,4);

// Button
const int button_pin = 16;
EasyButton button(button_pin, 100, false, false);

int brightnessLevels[] = {0x00,0x08,0x0f};
int brightnessLevel = brightnessLevels[2];

boolean brightnessAuto = true;
DateTime brightnessAutoTime;
DateTime now;

// NTPClient
WiFiUDP wifiUdp;
NTP ntp(wifiUdp);

// DHT
DHTesp dht;
float humidity;
float temperature;

// Webserver
ESP8266WebServer server(80);

// defaults
void connectWIFI();
void sendHTTP(const float &temperature, const float &humidity);
void getDHT22(bool sendhttp);
void checkBrightnessAuto(const DateTime& dt);
void syncNTP(const DateTime& dt);
void cycleBrightness(const DateTime& dt);
void onPressed();
void onPressedForDuration();
void showDate(const char* txt, const DateTime& dt);
String showDateString(const DateTime& dt);
void handle_NotFound();
void handle_OnConnect();
void handle_updateNTP();
String SendHTML(const DateTime& dt1, char* dt2);

void setup() {
  // Turn off wifi
  //WiFi.mode(WIFI_OFF);
  // Start Serial output 
  Serial.begin(9600);
  display.clear();
  display.setBrightness(brightnessLevel);
  display.clear();
  display.setSegments(SEG_BOOT);
  Serial.println();
  Serial.println();
  Serial.println();
  Serial.print("Booting... Compiled: ");
  Serial.print((__DATE__, __TIME__));
  Serial.println();
  
  // DEBUG Initial date & time
  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  //rtc.adjust(DateTime(2018, 01, 01, 01, 01, 0));
   // Initialize I2C  
  Wire.begin(); 
  if (rtc.begin()) {
    Serial.println("[RTC]: Started!");
  }
  
  // Check if RTC runs  
  if (rtc.lostPower()) {
    // If RTC is not running, set current Date and Time
    Serial.println("[RTC]: Will be restarted and set to System time of the sketch compilation.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    //rtc.adjust(DateTime(2016, 10, 31, 11, 45, 0));
    now = rtc.now();
  } else {
    //Serial.println("[RTC]: already running.");
    now = rtc.now();
  }
  showDate("[RTC]:", now);

  // DHT22
  dht.setup(13, DHTesp::DHT22);

  // WIFI
  connectWIFI();

  // Start NTP
  Serial.print("[NTP]: Starting ");
  ntp.ruleDST("CEST", Last, Sun, Mar, 2, 120); // last sunday in march 2:00, timezone +120min (+1 GMT + 1h summertime offset)
  ntp.ruleSTD("CET", Last, Sun, Oct, 3, 60); // last sunday in october 3:00, timezone +60min (+1 GMT)
  ntp.begin(false); // false means dont wait
  Serial.println(" OK");
  //ntp.ntpServer("0.it.pool.ntp.org");
  //ntp.updateInterval(30*60*1000); // 30 Minutes

  // Initialize the button.
  button.begin();
  button.onPressed(onPressed);
  button.onPressedFor(1000, onPressedForDuration);

  checkBrightnessAuto(now);

  // Start OTA
  ArduinoOTA.setHostname("AnnaUhr");
  // ArduinoOTA.setPassword("admin");
  ArduinoOTA.begin();
  Serial.println("[OTA]: Enabled");

  // Start Webserver
  server.on("/", handle_OnConnect);
  server.on("/updatentp", handle_updateNTP);
  server.onNotFound(handle_NotFound);
  server.begin();
  Serial.println("[HTTP]: Server started");
}

void loop() {
  DateTime now = rtc.now();

  /*if (now.second() == 0) {
    showDate("[RTC]:", now);
    delay(1000);
  }*/

  if (now.second() == 40) {
    checkBrightnessAuto(now);
    delay(1000);
  }

  /*if ((now.minute() == 0 || now.minute() == 10 || 
       now.minute() == 20 || now.minute() == 30 || 
       now.minute() == 40 || now.minute() == 50 ) && now.second() == 10) {*/
  if (now.second() == 10) {    
    getDHT22(true);
  }

  //if (now.minute() == 5 && now.second() == 2) {
  if (now.second() == 30) {
    syncNTP(now);
  }

  int t = now.hour() * 100 + now.minute();
  //Serial.println(t);
  if (now.second()%2 == 0){
    display.showNumberDec(t, true);
  } else {
    display.showNumberDecEx(t, (1 << 6), true);
  }

  server.handleClient();
  ArduinoOTA.handle();
  button.read();
}

void connectWIFI(){
  // WIFI
  if (WiFi.status() != WL_CONNECTED){
    WiFi.hostname("AnnaUhr");
    WiFi.config(ip_static,gateway,subnet,dns1,dns2);
    WiFi.begin("muhxnetwork", "Wombat2020");
    int i = 0;
    int tout = 40; // 60=30s
    Serial.print("[WiFi]: Connecting");
    while (i < tout){
      if (WiFi.status() == WL_CONNECTED){
        i = tout+1;
        Serial.println("OK");
        Serial.print("[WiFi]: IP: ");
        Serial.print(WiFi.localIP());
        Serial.print(", SM: ");
        Serial.print(WiFi.subnetMask());
        Serial.print(", GW: ");
        Serial.print(WiFi.gatewayIP());
        Serial.print(", DNS1: ");
        Serial.print(WiFi.dnsIP());
        Serial.print(", DNS2: ");
        Serial.println(WiFi.dnsIP(1)); 
      } else {
        delay(500);
        Serial.print(".");
        ++i;
      }
    }
    if (i == tout) {
      Serial.println(" ERROR");
    }
  } else {
    Serial.println("WIFI: Already connected");
  }
}

void sendHTTP(const float &temperature, const float &humidity){ 
      WiFiClient client;
      HTTPClient http;
      Serial.print("HTTP: Sending ");
      String httpaddress = "http://192.168.22.9/insertDB.php?type=dht22&id=2003&val1=";
      httpaddress += temperature;
      httpaddress += "&val2=";
      httpaddress += humidity;
      Serial.println(httpaddress);
      Serial.print("HTTP: Sending ");
      if (http.begin(client, httpaddress)) {
        int httpCode = http.GET();
        if (httpCode > 0) {
          Serial.print("GET code: ");
          Serial.print(httpCode);
          //Serial.printf("GET... code: %d\n", httpCode);
          if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            //String payload = http.getString();
            //Serial.println(payload);
            Serial.println(" success");
          }
        } else {
            Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
      } else {
        Serial.printf("[HTTP} Unable to connect\n");
      }
}

void getDHT22(bool sendhttp) {
    Serial.print("[DHT]: ");
    delay(dht.getMinimumSamplingPeriod());
    if (dht.getStatusString() == "OK") {
      humidity = dht.getHumidity();
      temperature = dht.getTemperature();
      Serial.println(dht.getStatusString());
      Serial.print("[DHT]: ");
      Serial.print(temperature, 1);
      Serial.print("C ");
      Serial.print(humidity, 1);
      Serial.println("%");
      if (sendhttp) {
        sendHTTP(temperature, humidity);
      }

      display.clear();
      display.setSegments(SEG_CEL);
      display.showNumberDec(int(temperature + 0.5), false, 3, 1);
      delay(5000); // display temp 
      display.clear();
      display.setSegments(SEG_HUM);
      display.showNumberDec(int(humidity + 0.5), false, 3, 1);
      delay(5000); // display temp
      display.clear();
    } else {
      Serial.println(dht.getStatusString());
    }
}

void checkBrightnessAuto(const DateTime& dt) {
  if (brightnessAuto){
    Serial.println("[AUTOMODE]: ON"); 
    if (dt.hour() >= 7 && dt.hour() <= 20){
      if (brightnessLevel != brightnessLevels[2]){
        display.setBrightness(brightnessLevels[2]);
        brightnessLevel = brightnessLevels[2];
        Serial.println("Brightness HIGH"); 
      }     
    } else {
      if (brightnessLevel != brightnessLevels[1]){
        display.setBrightness(brightnessLevels[1],true);
        brightnessLevel = brightnessLevels[1];
        Serial.println("Brightness LOW"); 
      }
    }  
    
  } else {
    Serial.println("[AUTOMODE]: OFF");
    if (dt.hour() > brightnessAutoTime.hour()){
      brightnessAuto = true;      
    } else {
      brightnessAuto = false;
    }
    
  }
}

void syncNTP(const DateTime& dt) {
  Serial.print("[NTP]: Syncing ");
  //if (ntp.update()) {
    ntp.update();
    if (ntp.year() != 1970) {
      rtc.adjust(DateTime(ntp.year(),ntp.month(),ntp.day(),ntp.hours(),ntp.minutes(),ntp.seconds()));
      Serial.println("success");
    } else {
      Serial.println("failure wrong year");  
    }
  //} else {
  //  Serial.println("failure");
  //}
  showDate("[RTC]:", dt);
  Serial.print("[NTP]: ");
  Serial.println(ntp.formattedTime("%Y/%m/%d %T"));
  delay(1000); // wait a bit
}

void cycleBrightness(const DateTime& dt) {
    Serial.println("BUTTON: Manual Mode");
    Serial.print("BUTTON: ");
    if (brightnessLevel == brightnessLevels[0]){
      display.setBrightness(brightnessLevels[1],true);
      brightnessLevel = brightnessLevels[1];
      Serial.println("Brightness LOW");
    } else if (brightnessLevel == brightnessLevels[1]){
      display.setBrightness(brightnessLevels[2]);
      brightnessLevel = brightnessLevels[2];
      Serial.println("Brightness HIGH");
    } else if (brightnessLevel == brightnessLevels[2]){
      display.setBrightness(brightnessLevels[0],false);
      brightnessLevel = brightnessLevels[0];
      Serial.println("Brightness OFF");
    }
    brightnessAuto = false;
    brightnessAutoTime = DateTime(dt.year(), dt.month(), dt.day(), dt.hour()+3, dt.minute(), dt.second());
    
}

void onPressed() {
    Serial.println("BUTTON: short press");
    cycleBrightness(now);
}

void onPressedForDuration() {
    Serial.println("BUTTON: long press");
    getDHT22(false);
}

void showDate(const char* txt, const DateTime& dt) {
    Serial.print(txt);
    Serial.print(' ');
    Serial.print(dt.year(), DEC);
    Serial.print('/');
    if(dt.month()<10){ Serial.print('0');}
    Serial.print(dt.month(), DEC);
    Serial.print('/');
    if(dt.day()<10){ Serial.print('0');}
    Serial.print(dt.day(), DEC);
    Serial.print(' ');
    if(dt.hour()<10){ Serial.print('0');}
    Serial.print(dt.hour(), DEC);
    Serial.print(':');
    if(dt.minute()<10){ Serial.print('0');}
    Serial.print(dt.minute(), DEC);
    Serial.print(':');
    if(dt.second()<10){ Serial.print('0');}
    Serial.print(dt.second(), DEC);
    Serial.println();
}

String showDateString(const DateTime& dt) {
    String ptr = "";
    ptr +=dt.year();
    ptr +="/";
    if(dt.month()<10){ ptr +="0";}
    ptr +=dt.month();
    ptr +="/";
    if(dt.day()<10){ ptr +="0";}
    ptr +=dt.day();
    ptr +=" ";
    if(dt.hour()<10){ ptr +="0";}
    ptr +=dt.hour();
    ptr +=":";
    if(dt.minute()<10){ ptr +="0";}
    ptr +=dt.minute();
    ptr +=":";
    if(dt.second()<10){ ptr +="0";}
    ptr +=dt.second();
    ptr +="\n";
    return ptr;
}

void handle_NotFound(){
  server.send(404, "text/plain", "Not found");
}

void handle_OnConnect() {
  DateTime now = rtc.now();
  server.send(200, "text/html", SendHTML(now, ntp.formattedTime("%Y/%m/%d %T"))); 
}

void handle_updateNTP() {
  Serial.println("[NTP]: Manual update");
  DateTime now = rtc.now();
  syncNTP(now);
  server.send(200, "text/html", SendHTML(now, ntp.formattedTime("%Y/%m/%d %T")));
}

String SendHTML(const DateTime& dt1, char* dt2){
  String ptr = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\"/><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/><link rel=\"stylesheet\" href=\"https://stackpath.bootstrapcdn.com/bootstrap/5.0.0-alpha2/css/bootstrap.min.css\" integrity=\"sha384-DhY6onE6f3zzKbjUPRc2hOzGAdEf4/Dz+WJwBvEYL/lkkIsI3ihufq9hk9K4lVoK\" crossorigin=\"anonymous\"><title>";
  ptr += WiFi.hostname();
  ptr += "</title></head><body> <nav class=\"navbar navbar-expand navbar-dark text-white\" style=\"background-color: #000;\"><div class=\"container px-1\" style=\"max-width: 360px !important\"> <a class=\"btn btn-sm btn-outline-light\" href=\"http://192.168.22.5/\"> <svg width=\"1em\" height=\"1em\" viewBox=\"0 0 16 16\" class=\"bi bi-front\" fill=\"currentColor\" xmlns=\"http://www.w3.org/2000/svg\"> <path fill-rule=\"evenodd\" d=\"M0 2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v2h2a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2v-2H2a2 2 0 0 1-2-2V2zm5 10v2a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1V6a1 1 0 0 0-1-1h-2v5a2 2 0 0 1-2 2H5z\"/> </svg> </a> <button class=\"navbar-toggler\" type=\"button\" data-toggle=\"collapse\" data-target=\"#navbarNav\" aria-controls=\"navbarNav\" aria-expanded=\"false\" aria-label=\"Toggle navigation\"> <span class=\"navbar-toggler-icon\"/> </button><div class=\"collapse navbar-collapse\" id=\"navbarNav\"><ul class=\"navbar-nav m-auto\"><li class=\"nav-item\"> <a class=\"nav-link active font-weight-bold\" aria-current=\"page\" href=\"http://192.168.22.55/\">";
  ptr += WiFi.hostname();
  ptr += "</a></li></ul> <a class=\"btn btn-sm btn-outline-light\" aria-current=\"page\" href=\"#\" onclick=\"location.reload(true)\"> <svg width=\"1em\" height=\"1em\" viewBox=\"0 0 16 16\" class=\"bi bi-arrow-clockwise\" fill=\"currentColor\" xmlns=\"http://www.w3.org/2000/svg\"> <path fill-rule=\"evenodd\" d=\"M8 3a5 5 0 1 0 4.546 2.914.5.5 0 0 1 .908-.417A6 6 0 1 1 8 2v1z\"/> <path d=\"M8 4.466V.534a.25.25 0 0 1 .41-.192l2.36 1.966c.12.1.12.284 0 .384L8.41 4.658A.25.25 0 0 1 8 4.466z\"/> </svg> </a></div></div> </nav><div class=\"container\" style=\"max-width: 360px !important\"><div class=\"row my-3 h1 bg-light border rounded\"><div class=\"col text-right\">";
  ptr += temperature;
  ptr += "&deg;</div><div class=\"col\">";
  ptr += humidity;
  ptr += "%</div></div><div class=\"row border rounded-top bg-light\"><div class=\"col\">RTC:</div><div class=\"col\">";
  ptr += showDateString(dt1);
  ptr += "</div></div><div class=\"row border border-top-0\"><div class=\"col\">NTP:</div><div class=\"col\">";
  ptr += dt2;
  ptr += "</div></div><div class=\"row mb-3\"><div class=\"col text-right p-0\"> <a class=\"btn btn-primary btn-sm btn-block\" href=\"/updatentp\" role=\"button\">Update</a></div></div><div class=\"row border rounded-top bg-light small\"><div class=\"col\">HN:</div><div class=\"col font-weight-bold\">";
  ptr += WiFi.hostname();
  ptr += "</div></div><div class=\"row border border-top-0 small\"><div class=\"col\">IP:</div><div class=\"col\">";
  ptr += WiFi.localIP().toString().c_str();
  ptr += "</div></div><div class=\"row border border-top-0 bg-light small\"><div class=\"col\">SM:</div><div class=\"col\">";
  ptr += WiFi.subnetMask().toString().c_str();
  ptr += "</div></div><div class=\"row border border-top-0 small\"><div class=\"col\">GW:</div><div class=\"col\">";
  ptr += WiFi.gatewayIP().toString().c_str();
  ptr += "</div></div><div class=\"row border border-top-0 bg-light small\"><div class=\"col\">DNS1:</div><div class=\"col\">";
  ptr += WiFi.dnsIP().toString().c_str();
  ptr += "</div></div><div class=\"row border border-top-0 rounded-bottom small\"><div class=\"col\">DNS2:</div><div class=\"col\">";
  ptr += WiFi.dnsIP(1).toString().c_str();
  ptr += "</div></div></div> <footer class=\"footer fixed-bottom py-1 bg-light\"><div class=\"container mt-auto px-1 text-center\" style=\"max-width: 360px !important\"> <span id=\"timedate\" class=\"text-muted small\"></span></div> </footer> <script>var d=new Date();document.getElementById(\"timedate\").innerHTML=d.toString();</script> <script src=\"https://stackpath.bootstrapcdn.com/bootstrap/5.0.0-alpha2/js/bootstrap.bundle.min.js\" integrity=\"sha384-BOsAfwzjNJHrJ8cZidOg56tcQWfp6y72vEJ8xQ9w6Quywb24iOsW913URv1IS4GD\" crossorigin=\"anonymous\"/></body></html>";
  return ptr;
}