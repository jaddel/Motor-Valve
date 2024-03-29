/****************************************************************************************************************************
  
  ESPAsync_WiFiManager is a library for the ESP8266/Arduino platform, using (ESP)AsyncWebServer to enable easy
  configuration and reconfiguration of WiFi credentials using a Captive Portal.

  Modified from
  1. Tzapu               (https://github.com/tzapu/WiFiManager)
  2. Ken Taylor          (https://github.com/kentaylor)
  3. Alan Steremberg     (https://github.com/alanswx/ESPAsyncWiFiManager)
  4. Khoi Hoang          (https://github.com/khoih-prog/ESP_WiFiManager)

  Built by Khoi Hoang https://github.com/khoih-prog/ESPAsync_WiFiManager
  Licensed under MIT license
 *****************************************************************************************************************************/
/****************************************************************************************************************************
   This example will open a configuration portal when the reset button is pressed twice.
   This method works well on Wemos boards which have a single reset button on board. It avoids using a pin for launching the configuration portal.

   How It Works
   1) ESP8266
   Save data in RTC memory
   2) ESP32
   Save data in EEPROM from address 256, size 512 bytes (both configurable)

   So when the device starts up it checks this region of ram for a flag to see if it has been recently reset.
   If so it launches a configuration portal, if not it sets the reset flag. After running for a while this flag is cleared so that
   it will only launch the configuration portal in response to closely spaced resets.

   Settings
   There are two values to be set in the sketch.

 *****************************************************************************************************************************/

#if !( defined(ESP8266) ||  defined(ESP32) )
  #error This code is intended to run on the ESP8266 or ESP32 platform! Please check your Tools->Board setting.
#endif

#define ESP_ASYNC_WIFIMANAGER_VERSION_MIN_TARGET      "ESPAsync_WiFiManager v1.12.2"
#define ESP_ASYNC_WIFIMANAGER_VERSION_MIN             1012002

// Use from 0 to 4. Higher number, more debugging messages and memory usage.
#define _ESPASYNC_WIFIMGR_LOGLEVEL_    1

#include <FS.h>
//#include <SPI.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//One Wire Dallas Temperature
#include <OneWire.h>
#include <DallasTemperature.h>

#include <TaskScheduler.h>

// For a connection via I2C using the Arduino Wire include:
//#include <Wire.h>               // Only needed for Arduino 1.6.5 and earlier



// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 26
#define RELAIS_BOILER 16
#define RELAIS_SOLAR 4
#define FEEDBACK_BOILER 33
#define FEEDBACK_SOLAR 14
#define SDA_PIN 21
#define SCL_PIN 17
#define BUTTON_PIN 23

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

const int PIN_LED = 2; // D4 on NodeMCU and WeMos. GPIO2/ADC12 of ESP32. Controls the onboard LED.

#define LED_ON            HIGH
#define LED_OFF           LOW

#define RLY_ON  LOW
#define RLY_OFF HIGH

#define CHECK_TIME 500 //periodical check time in ms

bool i_boiler;
bool i_solar;
bool q_boiler = false;
bool q_solar = false;
bool button_state = false;
bool last_button_state = false;
uint16_t menue_select = 0;
uint16_t button_time = 0;
uint8_t wificheck = 0;

bool manual_mode = false;
uint16_t runtime = 0;

// arrays to hold device addresses
DeviceAddress solarThermometer = { 0x28, 0x53, 0xB7, 0x99, 0xA0, 0x21, 0x1, 0xA8 }; //blue mark on device
DeviceAddress boilerThermometer = { 0x28, 0xFF, 0x64, 0x1E, 0x85, 0xEB, 0xD2, 0x81 }; //red mark on devide
// # ROM = 28 53 B7 99 A0 21 1 A8
// # ROM = 28 FF 64 1E 85 EB D2 81
//Display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

const char WM_HTTP_HEAD_CFG[] PROGMEM     = "<!DOCTYPE html><html lang='en'><head><meta name='viewport' content='width=device-width, initial-scale=1, user-scalable=no'/><META HTTP-EQUIV='refresh' CONTENT='10; URL=?' /><title>{v}</title>";

//Ported to ESP32

  #include <esp_wifi.h>
  #include <WiFi.h>
  #include <WiFiClient.h>

  // From v1.1.1
  #include <WiFiMulti.h>
  WiFiMulti wifiMulti;

  // LittleFS has higher priority than SPIFFS
  /*#if ( defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 2) )
    #define USE_LITTLEFS    true
    #define USE_SPIFFS      false
  #elif defined(ARDUINO_ESP32C3_DEV)
    // For core v1.0.6-, ESP32-C3 only supporting SPIFFS and EEPROM. To use v2.0.0+ for LittleFS
    #define USE_LITTLEFS          false
    #define USE_SPIFFS            true
  #endif*/

    #define USE_LITTLEFS    true
    #define USE_SPIFFS      false

  #if USE_LITTLEFS
    // Use LittleFS
    #include "FS.h"

    // Check cores/esp32/esp_arduino_version.h and cores/esp32/core_version.h
    //#if ( ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(2, 0, 0) )  //(ESP_ARDUINO_VERSION_MAJOR >= 2)
    #if ( defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 2) )
      #if (_ESPASYNC_WIFIMGR_LOGLEVEL_ > 3)
        #warning Using ESP32 Core 1.0.6 or 2.0.0+
      #endif
      
      // The library has been merged into esp32 core from release 1.0.6
      #include <LittleFS.h>       // https://github.com/espressif/arduino-esp32/tree/master/libraries/LittleFS
      
      FS* filesystem =      &LittleFS;
      #define FileFS        LittleFS
      #define FS_Name       "LittleFS"
    #else
      #if (_ESPASYNC_WIFIMGR_LOGLEVEL_ > 3)
        #warning Using ESP32 Core 1.0.5-. You must install LITTLEFS library
      #endif
   
      // The library has been merged into esp32 core from release 1.0.6
      #include <LITTLEFS.h>       // https://github.com/lorol/LITTLEFS
      
      FS* filesystem =      &LITTLEFS;
      #define FileFS        LITTLEFS
      #define FS_Name       "LittleFS"
    #endif
    
  #elif USE_SPIFFS
    #include <SPIFFS.h>
    FS* filesystem =      &SPIFFS;
    #define FileFS        SPIFFS
    #define FS_Name       "SPIFFS"
  #else
    // +Use FFat
    #include <FFat.h>
    FS* filesystem =      &FFat;
    #define FileFS        FFat
    #define FS_Name       "FFat"
  #endif

  #define ESP_getChipId()   ((uint32_t)ESP.getEfuseMac())

// SSID and PW for Config Portal
String ssid = "ESP_" + String(ESP_getChipId(), HEX);
String password;
//const char* password = "your_password";

// SSID and PW for your Router
String Router_SSID;
String Router_Pass;

// From v1.1.1
// You only need to format the filesystem once
//#define FORMAT_FILESYSTEM       true
#define FORMAT_FILESYSTEM         false

#define MIN_AP_PASSWORD_SIZE    8

#define SSID_MAX_LEN            32
//From v1.0.10, WPA2 passwords can be up to 63 characters long.
#define PASS_MAX_LEN            64

typedef struct
{
  char wifi_ssid[SSID_MAX_LEN];
  char wifi_pw  [PASS_MAX_LEN];
}  WiFi_Credentials;

typedef struct
{
  String wifi_ssid;
  String wifi_pw;
}  WiFi_Credentials_String;

#define NUM_WIFI_CREDENTIALS      2

// Assuming max 49 chars
#define TZNAME_MAX_LEN            50
#define TIMEZONE_MAX_LEN          50

typedef struct
{
  WiFi_Credentials  WiFi_Creds [NUM_WIFI_CREDENTIALS];
  char TZ_Name[TZNAME_MAX_LEN];     // "America/Toronto"
  char TZ[TIMEZONE_MAX_LEN];        // "EST5EDT,M3.2.0,M11.1.0"
  uint16_t checksum;
} WM_Config;

typedef struct
{
  uint16_t time; //in ms
  uint16_t offset; //in C
  uint16_t limit; //in C
} valve_Config; //pamaters for motor valve

WM_Config         WM_config;
valve_Config    valve_config;

float boiler_temp = 0;
float solar_temp = 0;


#define  CONFIG_FILENAME              F("/wifi_cred.dat")
//////

// Indicates whether ESP has WiFi credentials saved from previous session, or double reset detected
bool initialConfig = false;

// Use false if you don't like to display Available Pages in Information Page of Config Portal
// Comment out or use true to display Available Pages in Information Page of Config Portal
// Must be placed before #include <ESPAsync_WiFiManager.h>
#define USE_AVAILABLE_PAGES     true  //false

// From v1.0.10 to permit disable/enable StaticIP configuration in Config Portal from sketch. Valid only if DHCP is used.
// You'll loose the feature of dynamically changing from DHCP to static IP, or vice versa
// You have to explicitly specify false to disable the feature.
//#define USE_STATIC_IP_CONFIG_IN_CP          false

// Use false to disable NTP config. Advisable when using Cellphone, Tablet to access Config Portal.
// See Issue 23: On Android phone ConfigPortal is unresponsive (https://github.com/khoih-prog/ESP_WiFiManager/issues/23)
#define USE_ESP_WIFIMANAGER_NTP     false

// Just use enough to save memory. On ESP8266, can cause blank ConfigPortal screen
// if using too much memory
#define USING_AFRICA        false
#define USING_AMERICA       false
#define USING_ANTARCTICA    false
#define USING_ASIA          false
#define USING_ATLANTIC      false
#define USING_AUSTRALIA     false
#define USING_EUROPE        true
#define USING_INDIAN        false
#define USING_PACIFIC       false
#define USING_ETC_GMT       false

// Use true to enable CloudFlare NTP service. System can hang if you don't have Internet access while accessing CloudFlare
// See Issue #21: CloudFlare link in the default portal (https://github.com/khoih-prog/ESP_WiFiManager/issues/21)
#define USE_CLOUDFLARE_NTP          false

// New in v1.0.11
#define USING_CORS_FEATURE          true

////////////////////////////////////////////

// Use USE_DHCP_IP == true for dynamic DHCP IP, false to use static IP which you have to change accordingly to your network
#if (defined(USE_STATIC_IP_CONFIG_IN_CP) && !USE_STATIC_IP_CONFIG_IN_CP)
  // Force DHCP to be true
  #if defined(USE_DHCP_IP)
    #undef USE_DHCP_IP
  #endif
  #define USE_DHCP_IP     true
#else
  // You can select DHCP or Static IP here
  #define USE_DHCP_IP     true
  //#define USE_DHCP_IP     false
#endif

#if ( USE_DHCP_IP )
  // Use DHCP
  
  #if (_ESPASYNC_WIFIMGR_LOGLEVEL_ > 3)
    #warning Using DHCP IP
  #endif
  
  IPAddress stationIP   = IPAddress(0, 0, 0, 0);
  IPAddress gatewayIP   = IPAddress(192, 168, 2, 1);
  IPAddress netMask     = IPAddress(255, 255, 255, 0);
  
#else
  // Use static IP
  
  #if (_ESPASYNC_WIFIMGR_LOGLEVEL_ > 3)
    #warning Using static IP
  #endif
  
  #ifdef ESP32
    IPAddress stationIP   = IPAddress(192, 168, 2, 232);
  #else
    IPAddress stationIP   = IPAddress(192, 168, 2, 186);
  #endif
  
  IPAddress gatewayIP   = IPAddress(192, 168, 2, 1);
  IPAddress netMask     = IPAddress(255, 255, 255, 0);
#endif

////////////////////////////////////////////


#define USE_CONFIGURABLE_DNS      true

IPAddress dns1IP      = gatewayIP;
IPAddress dns2IP      = IPAddress(8, 8, 8, 8);

#define USE_CUSTOM_AP_IP          false

IPAddress APStaticIP  = IPAddress(192, 168, 100, 1);
IPAddress APStaticGW  = IPAddress(192, 168, 100, 1);
IPAddress APStaticSN  = IPAddress(255, 255, 255, 0);

#include <ESPAsync_WiFiManager.h>               //https://github.com/khoih-prog/ESPAsync_WiFiManager

// Redundant, for v1.10.0 only
//#include <ESPAsync_WiFiManager-Impl.h>          //https://github.com/khoih-prog/ESPAsync_WiFiManager

#define HTTP_PORT     80

//Webserver for later
AsyncWebServer webServer(HTTP_PORT);

DNSServer dnsServer;

String s = "MotorValve " + String(ESP_getChipId(), HEX);
ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer, s.c_str());

///////////////////////////////////////////
// New in v1.4.0
/******************************************
   // Defined in ESPAsync_WiFiManager.h
  typedef struct
  {
    IPAddress _ap_static_ip;
    IPAddress _ap_static_gw;
    IPAddress _ap_static_sn;
  }  WiFi_AP_IPConfig;

  typedef struct
  {
    IPAddress _sta_static_ip;
    IPAddress _sta_static_gw;
    IPAddress _sta_static_sn;
    #if USE_CONFIGURABLE_DNS
    IPAddress _sta_static_dns1;
    IPAddress _sta_static_dns2;
    #endif
  }  WiFi_STA_IPConfig;
******************************************/

WiFi_AP_IPConfig  WM_AP_IPconfig;
WiFi_STA_IPConfig WM_STA_IPconfig;

void initAPIPConfigStruct(WiFi_AP_IPConfig &in_WM_AP_IPconfig)
{
  in_WM_AP_IPconfig._ap_static_ip   = APStaticIP;
  in_WM_AP_IPconfig._ap_static_gw   = APStaticGW;
  in_WM_AP_IPconfig._ap_static_sn   = APStaticSN;
}

void initSTAIPConfigStruct(WiFi_STA_IPConfig &in_WM_STA_IPconfig)
{
  in_WM_STA_IPconfig._sta_static_ip   = stationIP;
  in_WM_STA_IPconfig._sta_static_gw   = gatewayIP;
  in_WM_STA_IPconfig._sta_static_sn   = netMask;
#if USE_CONFIGURABLE_DNS
  in_WM_STA_IPconfig._sta_static_dns1 = dns1IP;
  in_WM_STA_IPconfig._sta_static_dns2 = dns2IP;
#endif
}

void displayIPConfigStruct(WiFi_STA_IPConfig in_WM_STA_IPconfig)
{
  LOGERROR3(F("stationIP ="), in_WM_STA_IPconfig._sta_static_ip, ", gatewayIP =", in_WM_STA_IPconfig._sta_static_gw);
  LOGERROR1(F("netMask ="), in_WM_STA_IPconfig._sta_static_sn);
#if USE_CONFIGURABLE_DNS
  LOGERROR3(F("dns1IP ="), in_WM_STA_IPconfig._sta_static_dns1, ", dns2IP =", in_WM_STA_IPconfig._sta_static_dns2);
#endif
}

void configWiFi(WiFi_STA_IPConfig in_WM_STA_IPconfig)
{
#if USE_CONFIGURABLE_DNS
  // Set static IP, Gateway, Subnetmask, DNS1 and DNS2. New in v1.0.5
  WiFi.config(in_WM_STA_IPconfig._sta_static_ip, in_WM_STA_IPconfig._sta_static_gw, in_WM_STA_IPconfig._sta_static_sn, in_WM_STA_IPconfig._sta_static_dns1, in_WM_STA_IPconfig._sta_static_dns2);
#else
  // Set static IP, Gateway, Subnetmask, Use auto DNS1 and DNS2.
  WiFi.config(in_WM_STA_IPconfig._sta_static_ip, in_WM_STA_IPconfig._sta_static_gw, in_WM_STA_IPconfig._sta_static_sn);
#endif
}

///////////////////////////////////////////

uint8_t connectMultiWiFi()
{

  // For ESP32, this better be 0 to shorten the connect time.
  // For ESP32-S2/C3, must be > 500
  #if ( USING_ESP32_S2 || USING_ESP32_C3 )
    #define WIFI_MULTI_1ST_CONNECT_WAITING_MS           500L
  #else
    // For ESP32 core v1.0.6, must be >= 500
    #define WIFI_MULTI_1ST_CONNECT_WAITING_MS           800L
  #endif


#define WIFI_MULTI_CONNECT_WAITING_MS                   500L

  uint8_t status;

  WiFi.mode(WIFI_STA);

  LOGERROR(F("ConnectMultiWiFi with :"));

  if ( (Router_SSID != "") && (Router_Pass != "") )
  {
    LOGERROR3(F("* Flash-stored Router_SSID = "), Router_SSID, F(", Router_Pass = "), Router_Pass );
    LOGERROR3(F("* Add SSID = "), Router_SSID, F(", PW = "), Router_Pass );
    wifiMulti.addAP(Router_SSID.c_str(), Router_Pass.c_str());
  }

  for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
  {
    // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
    if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
    {
      LOGERROR3(F("* Additional SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
    }
  }

  LOGERROR(F("Connecting MultiWifi..."));

  //WiFi.mode(WIFI_STA);

#if !USE_DHCP_IP
  // New in v1.4.0
  configWiFi(WM_STA_IPconfig);
  //////
#endif
  
  status = wifiMulti.run();



  return status;
}

#if USE_ESP_WIFIMANAGER_NTP

void printLocalTime()
{

  struct tm timeinfo;

  getLocalTime( &timeinfo );

  // Valid only if year > 2000. 
  // You can get from timeinfo : tm_year, tm_mon, tm_mday, tm_hour, tm_min, tm_sec
  if (timeinfo.tm_year > 100 )
  {
    Serial.print("Local Date/Time: ");
    Serial.print( asctime( &timeinfo ) );
  }

}

#endif

void heartBeatPrint()
{
#if USE_ESP_WIFIMANAGER_NTP
  printLocalTime();
#else
  static int num = 1;

  if (WiFi.status() == WL_CONNECTED)
    Serial.print(F("H"));        // H means connected to WiFi
  else
    Serial.print(F("F"));        // F means not connected to WiFi

  if (num == 80)
  {
    Serial.println();
    num = 1;
  }
  else if (num++ % 10 == 0)
  {
    Serial.print(F(" "));
  }
#endif  
}

void check_WiFi()
{
  if ( (WiFi.status() != WL_CONNECTED) )
  {
    Serial.println(F("\nWiFi lost. Call connectMultiWiFi in loop"));
    connectMultiWiFi();
  }
}

void check_status()
{
  static ulong checkstatus_timeout  = 0;
  static ulong checkwifi_timeout    = 0;

  static ulong current_millis;

#define WIFICHECK_INTERVAL    1000L

#if USE_ESP_WIFIMANAGER_NTP
  #define HEARTBEAT_INTERVAL    60000L
#else
  #define HEARTBEAT_INTERVAL    10000L
#endif

  current_millis = millis();

  // Check WiFi every WIFICHECK_INTERVAL (1) seconds.
  if ((current_millis > checkwifi_timeout) || (checkwifi_timeout == 0))
  {
    check_WiFi();
    checkwifi_timeout = current_millis + WIFICHECK_INTERVAL;
  }

  // Print hearbeat every HEARTBEAT_INTERVAL (10) seconds.
  if ((current_millis > checkstatus_timeout) || (checkstatus_timeout == 0))
  {
    heartBeatPrint();
    checkstatus_timeout = current_millis + HEARTBEAT_INTERVAL;
  }
}

int calcChecksum(uint8_t* address, uint16_t sizeToCalc)
{
  uint16_t checkSum = 0;
  
  for (uint16_t index = 0; index < sizeToCalc; index++)
  {
    checkSum += * ( ( (byte*) address ) + index);
  }

  return checkSum;
}

bool loadConfigData()
{
  File file = FileFS.open(CONFIG_FILENAME, "r");
  LOGERROR(F("LoadWiFiCfgFile "));

  memset((void *) &WM_config,       0, sizeof(WM_config));
  memset((void *) &valve_config,       0, sizeof(valve_config));

  // New in v1.4.0
  memset((void *) &WM_STA_IPconfig, 0, sizeof(WM_STA_IPconfig));
  //////

  if (file)
  {
    file.readBytes((char *) &WM_config,   sizeof(WM_config));
    file.readBytes((char *) &valve_config,   sizeof(valve_config));

    // New in v1.4.0
    file.readBytes((char *) &WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
    //////

    file.close();
    LOGERROR(F("OK"));

    if ( WM_config.checksum != calcChecksum( (uint8_t*) &WM_config, sizeof(WM_config) - sizeof(WM_config.checksum) ) )
    {
      LOGERROR(F("WM_config checksum wrong"));

      return false;
    }
    
    // New in v1.4.0
    displayIPConfigStruct(WM_STA_IPconfig);
    //////

    return true;
  }
  else
  {
    LOGERROR(F("failed"));

    return false;
  }
}

void saveConfigData()
{
  File file = FileFS.open(CONFIG_FILENAME, "w");
  LOGERROR(F("SaveWiFiCfgFile "));

  if (file)
  {
    WM_config.checksum = calcChecksum( (uint8_t*) &WM_config, sizeof(WM_config) - sizeof(WM_config.checksum) );
    
    file.write((uint8_t*) &WM_config, sizeof(WM_config));
    file.write((uint8_t*) &valve_config, sizeof(valve_config));

    displayIPConfigStruct(WM_STA_IPconfig);

    // New in v1.4.0
    file.write((uint8_t*) &WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
    //////

    file.close();
    LOGERROR(F("OK"));
  }
  else
  {
    LOGERROR(F("failed"));
  }
}

// function to print the temperature for a device
float giveTemperature(DeviceAddress deviceAddress)
{
  float tempC = sensors.getTempC(deviceAddress);
  return tempC;
}

void handleWWWApp( AsyncWebServerRequest *request ){

  bool news = false;

  LOGDEBUG(F("Answer any"));
  
  String page = FPSTR(WM_HTTP_HEAD_CFG);
  page.replace("{v}", "Options");
  page += FPSTR(WM_HTTP_SCRIPT);
  page += FPSTR(WM_HTTP_SCRIPT_NTP);
  page += FPSTR(WM_HTTP_STYLE);
  
  page += FPSTR(WM_HTTP_HEAD_END);
  page += "<h2>";
  page += "Motor Valve Config page";
  page += "</h2>";

for (uint8_t i = 0; i < request->args(); i++)
  {
    page += " " + request->argName(i) + ": " + request->arg(i) + "\n";
    if(request->argName(i) == "time")
    {
    valve_config.time = request->arg(i).toInt();
    news = true;
    }
    
    if(request->argName(i) == "offset")
    {
    valve_config.offset = request->arg(i).toInt();
    news = true;
    }

    if(request->argName(i) == "limit")
    {
    valve_config.limit = request->arg(i).toInt();
    news = true;
    }

    if(request->argName(i) == "manual")
    manual_mode = true;

    if(request->argName(i) == "auto")
    {
    manual_mode = false;
    runtime = 0;
    }

    if(request->argName(i) == "manboiler")
    {
    q_boiler = true;
    q_solar = false;
    runtime = 0;
    }

    if(request->argName(i) == "mansolar")
    {
    q_boiler = false;
    q_solar = true;
    runtime = 0;
    }

    if(request->argName(i) == "manstop")
    {
    q_boiler = false;
    q_solar = false;
    runtime = 0;
    }

  }

  if(news)
  saveConfigData();

  //Parameter config
  page += "<form action=\"/\"> ";

  page += "<table>";

  page += "<tr>";

  page += "<td>";
  page += "Mode: ";
  page += "</td>";

  page += "<td>";
  page += (manual_mode)?"Manual Mode":"Automatic Mode";
  page += "</td>";

  page += "</tr>";




  page += "<tr>";

  page += "<td>";
  page += "Valve position: ";
  page += "</td>";

  page += "<td>";
  page +=  (i_boiler)?"Boiler ":"";
  page +=  (i_solar)?"Solar":"";
  page += "</td>";

  page += "</tr>";

  page += "<tr>";

  page += "<td>";
  page += "Valve actuator: ";
  page += "</td>";

  page += "<td>";
  page +=  (q_boiler)?"Boiler ":"";
  page +=  (q_solar)?"Solar":"";
  page += "</td>";

  page += "</tr>";

  page += "<tr>";

  page += "<td>";
  page += "Boiler Temperature: [C]";
  page += "</td>";
  
  page += "<td>";
  page += String(boiler_temp) ;
  page += "</td>";

  page += "</tr>";
  page += "<tr>";

  page += "<td>";
  page += "Solar Temperature: [C]";
  page += "</td>";
  
  page += "<td>";
  page += String(solar_temp) ;
  page += "</td>";

  page += "</tr>";

  page += "<tr>";

  page += "<td>";
  page += "max. runtime: [s]";
  page += "</td>";
  
  page += "<td>";
  page += "<input type=\"number\" name=\"time\" value=\"" + String(valve_config.time) + "\">";
  page += "Last: ";
  page += (runtime * CHECK_TIME) / 1000;
  page += "</td>";

  page += "</tr>";
  page += "<tr>";

  page += "<td>";
  page += "Solar limit temperature: [C]";
  page += "</td>";
  
  page += "<td>";
  page += "<input type=\"number\" name=\"limit\" value=\"" + String(valve_config.limit) + "\">";
  page += "</td>";

  page += "</tr>";
  page += "<tr>";

  page += "<td>";
  page += "Offset temperature: [C]";
  page += "</td>";

  page += "<td>";
  page += "<input type=\"number\" name=\"offset\" value=\"" + String(valve_config.offset) + "\"> ";
  page += "</td>";

  page += "</tr>";

  page += "<tr>";
  page += "<td colspan=2>";
  page += "Description: <ul><li>Valve changes to Solar mode if the Solar temperature exceeds the boiler temperature by the offset temperaure</li>";
  page += "<li>Valve changes to Solar mode if the Solar temperature exceeds solar limit temperature by the offset temperature</li>";
  page += "<li>Valve changes to Boiler mode if the solar temperature is below the solar limit temperature and below the boiler temperature</li>";
  page += "</ul>";
  page += "</td>";
  page += "</tr>";

  page += "<tr>";
  page += "<td>";
  page += "<input type=\"submit\" value=\"Save\"> ";
  page += "</td>";
  page += "</tr>";

  page += "</table>";

  page += "</form> ";

  //Manual Mode

  page += "<form action=\"/\"> ";
  page += "<input type=\"submit\" value=\"" ;
  page += (manual_mode)?"Automatic Mode":"Manual Mode";
  page += "\" ";
  page += "name=\"" ;
  page += (manual_mode)?"auto":"manual";
  page += "\"> ";

  if(manual_mode)
  {
    page += "<input type=\"submit\" value=\"manual Boiler\" name=\"manboiler\" >" ;
    page += "<input type=\"submit\" value=\"manual Solar\" name=\"mansolar\" >" ;
    page += "<input type=\"submit\" value=\"manual Stop\" name=\"manstop\" >" ;
  }

  page += "</form> ";

  page += FPSTR(WM_FLDSET_END);  
  page += FPSTR(WM_HTTP_END);

  AsyncWebServerResponse *response = request->beginResponse(200, WM_HTTP_HEAD_CT, page);
  response->addHeader(FPSTR(WM_HTTP_CACHE_CONTROL), FPSTR(WM_HTTP_NO_STORE));
  
  response->addHeader(FPSTR(WM_HTTP_PRAGMA), FPSTR(WM_HTTP_NO_CACHE));
  response->addHeader(FPSTR(WM_HTTP_EXPIRES), "-1");
  
  request->send(response);

}

// function to print a device address
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}


// function to print the temperature for a device
void printTemperature(DeviceAddress deviceAddress)
{
  float tempC = sensors.getTempC(deviceAddress);
  Serial.print("Temp C: ");
  Serial.print(tempC);
}

void printAlarms(uint8_t deviceAddress[])
{
  char temp;
  temp = sensors.getHighAlarmTemp(deviceAddress);
  Serial.print("High Alarm: ");
  Serial.print(temp, DEC);
  Serial.print("C/");
  Serial.print(DallasTemperature::toFahrenheit(temp));
  Serial.print("F | Low Alarm: ");
  temp = sensors.getLowAlarmTemp(deviceAddress);
  Serial.print(temp, DEC);
  Serial.print("C/");
  Serial.print(DallasTemperature::toFahrenheit(temp));
  Serial.print("F");
}

// main function to print information about a device
void printData(DeviceAddress deviceAddress)
{
  Serial.print("Device Address: ");
  printAddress(deviceAddress);
  Serial.print(" ");
  printTemperature(deviceAddress);
  Serial.println();
}




void checker();
Scheduler runner;
// We create the task indicating that it runs every 500 milliseconds, forever
Task checkerTask(CHECK_TIME, -1, &checker);

void setup()
{
  // put your setup code here, to run once:
  // initialize the LED digital pin as an output.
  pinMode(PIN_LED, OUTPUT);

  pinMode(RELAIS_BOILER, OUTPUT);     
  pinMode(RELAIS_SOLAR, OUTPUT);     
  pinMode(FEEDBACK_BOILER, INPUT_PULLUP);   
  pinMode(FEEDBACK_SOLAR, INPUT_PULLUP);   
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  digitalWrite( RELAIS_BOILER, RLY_OFF );
  digitalWrite( RELAIS_SOLAR, RLY_OFF );

  Wire.setPins(SDA_PIN, SCL_PIN);

  Serial.begin(115200);
  while (!Serial);

  delay(200);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  display.setRotation(2);
  
  // Clear the buffer
  display.clearDisplay();

  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  display.write("MotorValve booting \n");
  display.display();
  
  // We add the task to the task scheduler
  runner.addTask(checkerTask);

  // We activate the task
  checkerTask.enable();

  display.write("Start Thermometer... \n");
  display.display();
  // show the addresses we found on the bus
  Serial.print("Device 0 Address: ");
  printAddress(solarThermometer);
  Serial.println();

  Serial.print("Device 0 Alarms: ");
  printAlarms(solarThermometer);
  Serial.println();
  
  Serial.print("Device 1 Address: ");
  printAddress(boilerThermometer);
  Serial.println();

  Serial.print("Device 1 Alarms: ");
  printAlarms(boilerThermometer);
  Serial.println();

  Serial.print("Device 0 Temperature: ");
  Serial.print(giveTemperature(boilerThermometer));
  Serial.print("Device 1 Temperature: ");
  Serial.print(giveTemperature(solarThermometer));


  /*Serial.print(F("\nStarting Async_ConfigOnDoubleReset using ")); Serial.print(FS_Name);
  Serial.print(F(" on ")); Serial.println(ARDUINO_BOARD);
  Serial.println(ESP_ASYNC_WIFIMANAGER_VERSION);
  Serial.println(ESP_DOUBLE_RESET_DETECTOR_VERSION);*/

#if defined(ESP_ASYNC_WIFIMANAGER_VERSION_INT)
  if (ESP_ASYNC_WIFIMANAGER_VERSION_INT < ESP_ASYNC_WIFIMANAGER_VERSION_MIN)
  {
    Serial.print("Warning. Must use this example on Version later than : ");
    Serial.println(ESP_ASYNC_WIFIMANAGER_VERSION_MIN_TARGET);
  }
#endif

  Serial.setDebugOutput(false);

  display.write("Init LittleFS... \n");
  display.display();
  if (FORMAT_FILESYSTEM)
    FileFS.format();

  // Format FileFS if not yet

  if (!FileFS.begin(true))

  {


    Serial.println(F("SPIFFS/LittleFS failed! Already tried formatting."));
  
    if (!FileFS.begin())
    {     
      // prevents debug info from the library to hide err message.
      delay(100);
      
#if USE_LITTLEFS
      Serial.println(F("LittleFS failed!. Please use SPIFFS or EEPROM. Stay forever"));
#else
      Serial.println(F("SPIFFS failed!. Please use LittleFS or EEPROM. Stay forever"));
#endif

      while (true)
      {
        delay(1);
      }
    }
  }
  
  display.write("Init Wifi");
  display.display();

  //drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);

  unsigned long startedAt = millis();

  // New in v1.4.0
  initAPIPConfigStruct(WM_AP_IPconfig);
  initSTAIPConfigStruct(WM_STA_IPconfig);
  

  //Local intialization. Once its business is done, there is no need to keep it around
  // Use this to default DHCP hostname to ESP8266-XXXXXX or ESP32-XXXXXX
  //ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer);
  // Use this to personalize DHCP hostname (RFC952 conformed)
 // AsyncWebServer webServer(HTTP_PORT);


  //DNSServer dnsServer;

  //ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer, "AsyncConfigOnDoubleReset");


#if USE_CUSTOM_AP_IP
  //set custom ip for portal
  // New in v1.4.0
  ESPAsync_wifiManager.setAPStaticIPConfig(WM_AP_IPconfig);
  //////
#endif

  ESPAsync_wifiManager.setMinimumSignalQuality(-1);

  // From v1.0.10 only
  // Set config portal channel, default = 1. Use 0 => random channel from 1-11
  ESPAsync_wifiManager.setConfigPortalChannel(0);
  //////

#if !USE_DHCP_IP
  // Set (static IP, Gateway, Subnetmask, DNS1 and DNS2) or (IP, Gateway, Subnetmask). New in v1.0.5
  // New in v1.4.0
  ESPAsync_wifiManager.setSTAStaticIPConfig(WM_STA_IPconfig);
  //////
#endif

  // New from v1.1.1
#if USING_CORS_FEATURE
  ESPAsync_wifiManager.setCORSHeader("Your Access-Control-Allow-Origin");
#endif

  // We can't use WiFi.SSID() in ESP32 as it's only valid after connected.
  // SSID and Password stored in is wifi_ap_record_t and wifi_config_t are also cleared in reboot
  // Have to create a new function to store in EEPROM/SPIFFS for this purpose
  Router_SSID = ESPAsync_wifiManager.WiFi_SSID();
  Router_Pass = ESPAsync_wifiManager.WiFi_Pass();

  //Remove this line if you do not want to see WiFi password printed
  Serial.println("ESP Self-Stored: SSID = " + Router_SSID + ", Pass = " + Router_Pass);

  // SSID to uppercase
  ssid.toUpperCase();
  password   = "My" + ssid;

  bool configDataLoaded = false;

  // From v1.1.0, Don't permit NULL password
  if ( (Router_SSID != "") && (Router_Pass != "") )
  {
    LOGERROR3(F("* Add SSID = "), Router_SSID, F(", PW = "), Router_Pass);
    wifiMulti.addAP(Router_SSID.c_str(), Router_Pass.c_str());

    ESPAsync_wifiManager.setConfigPortalTimeout(120); //If no access point name has been previously entered disable timeout.
    Serial.println(F("Got ESP Self-Stored Credentials. Timeout 120s for Config Portal"));
  }
  
  if (loadConfigData())
  {
    configDataLoaded = true;

    ESPAsync_wifiManager.setConfigPortalTimeout(120); //If no access point name has been previously entered disable timeout.
    Serial.println(F("Got stored Credentials. Timeout 120s for Config Portal"));

#if USE_ESP_WIFIMANAGER_NTP      
    if ( strlen(WM_config.TZ_Name) > 0 )
    {
      LOGERROR3(F("Current TZ_Name ="), WM_config.TZ_Name, F(", TZ = "), WM_config.TZ);


      //configTzTime(WM_config.TZ, "pool.ntp.org" );
      configTzTime(WM_config.TZ, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
 
    }
    else
    {
      Serial.println(F("Current Timezone is not set. Enter Config Portal to set."));
    } 
#endif
  }
  else
  {
    // Enter CP only if no stored SSID on flash and file
    Serial.println(F("Open Config Portal without Timeout: No stored Credentials."));
    initialConfig = true;
  }

  //should be triggerd from extern
  /*if (drd->detectDoubleReset())
  {
    // DRD, disable timeout.
    ESPAsync_wifiManager.setConfigPortalTimeout(0);

    Serial.println(F("Open Config Portal without Timeout: Double Reset Detected"));
    initialConfig = true;
  }*/

  if (initialConfig )
  {
    /*Serial.print(F("Starting configuration portal @ "));
    
#if USE_CUSTOM_AP_IP    
    Serial.print(APStaticIP);
#else
    Serial.print(F("192.168.4.1"));
#endif

    Serial.print(F(", SSID = "));
    Serial.print(ssid);
    Serial.print(F(", PWD = "));
    Serial.println(password);

    digitalWrite(PIN_LED, LED_ON); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.

    //sets timeout in seconds until configuration portal gets turned off.
    //If not specified device will remain in configuration mode until
    //switched off via webserver or device is restarted.
    //ESPAsync_wifiManager.setConfigPortalTimeout(600);

    // Starts an access point
    if (!ESPAsync_wifiManager.startConfigPortal((const char *) ssid.c_str(), password.c_str()))
      Serial.println(F("Not connected to WiFi but continuing anyway."));
    else
    {
      Serial.println(F("WiFi connected...yeey :)"));
    }
*/
    // Stored  for later usage, from v1.1.0, but clear first
    memset(&WM_config, 0, sizeof(WM_config));
/*
    for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
    {
      String tempSSID = ESPAsync_wifiManager.getSSID(i);
      String tempPW   = ESPAsync_wifiManager.getPW(i);

      if (strlen(tempSSID.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1)
        strcpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str());
      else
        strncpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1);

      if (strlen(tempPW.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1)
        strcpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str());
      else
        strncpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1);

      // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
      if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
      {
        LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
        wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
      }
    }

#if USE_ESP_WIFIMANAGER_NTP      
    String tempTZ   = ESPAsync_wifiManager.getTimezoneName();

    if (strlen(tempTZ.c_str()) < sizeof(WM_config.TZ_Name) - 1)
      strcpy(WM_config.TZ_Name, tempTZ.c_str());
    else
      strncpy(WM_config.TZ_Name, tempTZ.c_str(), sizeof(WM_config.TZ_Name) - 1);

    const char * TZ_Result = ESPAsync_wifiManager.getTZ(WM_config.TZ_Name);
    
    if (strlen(TZ_Result) < sizeof(WM_config.TZ) - 1)
      strcpy(WM_config.TZ, TZ_Result);
    else
      strncpy(WM_config.TZ, TZ_Result, sizeof(WM_config.TZ_Name) - 1);
         
    if ( strlen(WM_config.TZ_Name) > 0 )
    {
      LOGERROR3(F("Saving current TZ_Name ="), WM_config.TZ_Name, F(", TZ = "), WM_config.TZ);


      //configTzTime(WM_config.TZ, "pool.ntp.org" );
      configTzTime(WM_config.TZ, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");

    }
    else
    {
      LOGERROR(F("Current Timezone Name is not set. Enter Config Portal to set."));
    }
#endif

    // New in v1.4.0
    ESPAsync_wifiManager.getSTAStaticIPConfig(WM_STA_IPconfig);
    //////

    saveConfigData();
    */
  }

  digitalWrite(PIN_LED, LED_OFF); // Turn led off as we are not in configuration mode.

  startedAt = millis();

  if (!initialConfig)
  {
    // Load stored data, the addAP ready for MultiWiFi reconnection
    if (!configDataLoaded)
      loadConfigData();

      display.write(".");
      display.display();

    for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
    {
      // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
      if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
      {
        LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
        wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
      }
    }

    if ( WiFi.status() != WL_CONNECTED )
    {
      Serial.println(F("ConnectMultiWiFi in setup"));

      uint8_t status = connectMultiWiFi();

        delay(WIFI_MULTI_1ST_CONNECT_WAITING_MS);

        uint8_t i;
        while ( ( i++ < 20 ) && ( status != WL_CONNECTED ) )
        {
          display.write(".");
          display.display();

          status = WiFi.status();

          if ( status == WL_CONNECTED )
            break;
          else
            delay(WIFI_MULTI_CONNECT_WAITING_MS);

        }

        if ( status == WL_CONNECTED )
        {
          LOGERROR1(F("WiFi connected after time: "), i);
          LOGERROR0(F("WiFi connected "));
          LOGERROR3(F("SSID:"), WiFi.SSID(), F(",RSSI="), WiFi.RSSI());
          LOGERROR3(F("Channel:"), WiFi.channel(), F(",IP address:"), WiFi.localIP() );
        }
        else
        {
          LOGERROR(F("WiFi not connected"));
          //  ESP.restart(); //Too much!
        }
    }

  }

  Serial.print(F("After waiting "));
  Serial.print((float) (millis() - startedAt) / 1000);
  Serial.print(F(" secs more in setup(), connection result is "));

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print(F("connected. Local IP: "));
    Serial.println(WiFi.localIP());   
  }
  else
    Serial.println(ESPAsync_wifiManager.getStatus(WiFi.status()));



//regular setup
  
  //webServer.reset();
  //webServer.onNotFound( std::bind(&handleWWWRoot() ) );
  //webServer.onNotFound (std::bind(&ESPAsync_WiFiManager::handleNotFound,        this, std::placeholders::_1));
  //webServer.onNotFound  ( &handleWWWRoot );
  webServer.on( "/", &handleWWWApp );
  webServer.begin(); // Web server start

}

void loop()
{
  // Call the double reset detector loop method every so often,
  // so that it can recognise when the timeout expires.
  // You can also call drd.stop() when you wish to no longer
  // consider the next reset as a double reset.
  //drd->loop();

    // It is necessary to run the runner on each loop
  runner.execute();

  //Read feedbacks
  i_boiler = digitalRead(FEEDBACK_BOILER);
  i_solar = digitalRead(FEEDBACK_SOLAR);

  button_state = !digitalRead(BUTTON_PIN);

  //Switch logic
  if(!manual_mode)
  {
    if(solar_temp + valve_config.offset > boiler_temp || solar_temp + valve_config.offset > valve_config.limit)
    {
    q_boiler = false;
    q_solar = true;
    }
    else if( solar_temp < boiler_temp && solar_temp < valve_config.limit )
    {
    q_solar = false;
    q_boiler = true;
    }
  }

  if(i_boiler){ // End position reached
  q_boiler = false;
  //runtime = 0;
  }

  if(i_solar){ //End position reached
  q_solar = false;
  //runtime = 0;
  }

  if((runtime * CHECK_TIME) / 1000 > valve_config.time  ) //if runtime is exceeded
  {
    q_boiler = false; //reset boiler actuator
    q_solar = false; //reset solar actuator
  }

  //Interlock
  if(q_boiler && !q_solar)
    digitalWrite( RELAIS_BOILER, RLY_ON );
  else
    digitalWrite( RELAIS_BOILER, RLY_OFF );

  if(q_solar && !q_boiler)
    digitalWrite( RELAIS_SOLAR, RLY_ON );
  else
    digitalWrite( RELAIS_SOLAR, RLY_OFF ); 

  //Button management
    if(button_time > 1500)
    { //menue_select = 5; //only for debug
      if (menue_select == 1) //stopp all
      {
        manual_mode = 0;
        menue_select = 0;
      }
      else if (menue_select == 2)
      {
        manual_mode = 1;
        q_boiler = 1;
        q_solar = 0;
        menue_select = 0;
      }
      else if (menue_select == 3)
      {
        manual_mode = 1;
        q_boiler = 0;
        q_solar = 1;
        menue_select = 0;
      }
      else if (menue_select == 5)
      {
          //Disable Motor. No Program active now!
          digitalWrite( RELAIS_BOILER, RLY_OFF );
          digitalWrite( RELAIS_SOLAR, RLY_OFF );

          display.clearDisplay();
          display.setCursor(0, 0);     // Start at top-left corner
          display.write("Open Wifi AP\n");
          display.write("192.168.4.1\n");
          display.write("SSID=");
          display.write(ssid.c_str());
          display.write("\n");
          display.write("PWD=");
          display.write(password.c_str());
          display.display();

          digitalWrite(PIN_LED, LED_ON); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.

          Serial.println("before");
          for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
          {  
            Serial.println(WM_config.WiFi_Creds[i].wifi_ssid);
            Serial.println(WM_config.WiFi_Creds[i].wifi_pw);
            
          }

          // Starts an access point
          if (!ESPAsync_wifiManager.startConfigPortal((const char *) ssid.c_str(), password.c_str()))
            Serial.println(F("Not connected to WiFi but continuing anyway."));
          else
          {
            Serial.println(F("WiFi connected...yeey :)"));
          }

          // Stored  for later usage, from v1.1.0, but clear first
          //memset(&WM_config, 0, sizeof(WM_config)); //would delete data.
          //WM_config is already filled in setup

          if(strlen(ESPAsync_wifiManager.getSSID(0).c_str()) > 0) //There must be data.
          {
          for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
          {
            String tempSSID = ESPAsync_wifiManager.getSSID(i);
            String tempPW   = ESPAsync_wifiManager.getPW(i);

            if (strlen(tempSSID.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1)
              strcpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str());
            else
              strncpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1);

            if (strlen(tempPW.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1)
              strcpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str());
            else
              strncpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1);

            // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
            if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
            {
              LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
              wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
            }
          }

          // New in v1.4.0
          ESPAsync_wifiManager.getSTAStaticIPConfig(WM_STA_IPconfig);
          //////


          saveConfigData();
          }
          

          
          Serial.println("after");
          for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
          {  
            Serial.println(WM_config.WiFi_Creds[i].wifi_ssid);
            Serial.println(WM_config.WiFi_Creds[i].wifi_pw);
          }

          digitalWrite(PIN_LED, LED_OFF);

        //ESP.restart(); //Best option

        menue_select = 0;

      }
      else if (menue_select == 6)
      {
        ESP.restart();
      }
      else if (menue_select == 4 || menue_select == 8)
      {
        menue_select = 0;
      }
    }
    else if (!button_state && last_button_state)
    {
      //short press
      menue_select ++;
      if(menue_select > 8)
      menue_select = 1;
    }

  //Config Menue
  if(menue_select > 0)
  {
  //Display control

  display.clearDisplay();

  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  //display.write("Config Menue\n");
  if(menue_select <= 4 )
  {
  if(menue_select == 1) display.write(0x10); else display.write(" ");
  display.write("1: Automatic mode\n");
  if(menue_select == 2) display.write(0x10); else display.write(" ");
  display.write("2: manual Boiler\n");
  if(menue_select == 3) display.write(0x10); else display.write(" ");
  display.write("3: manual Solar\n");
  if(menue_select == 4) display.write(0x10); else display.write(" ");
  display.write("4: Exit\n");
  }
  else
  {
  if(menue_select == 5) display.write(0x10); else display.write(" ");
  display.write("5: Wifi config\n");
  if(menue_select == 6) display.write(0x10); else display.write(" ");
  display.write("6: Reboot\n");
  if(menue_select == 7) display.write(0x10); else display.write(" ");
  display.write("7: \n");
  if(menue_select == 8) display.write(0x10); else display.write(" ");
  display.write("8: Exit\n");
  }

  display.display();

  }

  //Button management
  last_button_state = button_state;

  if(!button_state)
    button_time = 0;

}

//periodical checker
void checker()
{

  if(button_state)
    button_time += CHECK_TIME;
  
  
  if( (q_boiler || q_solar) && (runtime * CHECK_TIME) / 1000 <= valve_config.time)
  runtime++;

  sensors.requestTemperatures();

  Serial.print("Boiler: ");
  boiler_temp = giveTemperature(boilerThermometer);
  printTemperature(boilerThermometer);
  
  
  Serial.print(" Solar: ");
  solar_temp = giveTemperature(solarThermometer);
  printTemperature(solarThermometer);

  Serial.print(" Feedback: ");
  Serial.print( (i_boiler)?"Boiler":"");
  Serial.print( (i_solar)?"Solar":"" );
  Serial.println();


  if(menue_select == 0)
  {
  //Display control

  display.clearDisplay();

  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  if( q_solar || q_boiler)
  {
    //display.write("Pos ");
    if( q_boiler )
      display.write("Boiler ");

    if( q_solar )
      display.write("Solar ");

    display.write("targeted");
  }
  else if( i_solar ^ i_boiler)
  {
    display.write("Position ");
    if( i_solar )
      display.write("Solar ");

    if( i_boiler )
      display.write("Boiler ");

    //display.write("reached");
  }
  else
    display.write("Unknown position");
  
  char s [20];

  if (q_boiler || q_solar)
  {
  display.write("\nRuntime: ");
  sprintf(s, "%d",runtime * CHECK_TIME/1000);
  display.write(s);
  display.write("/");
  sprintf(s, "%d",valve_config.time);
  display.write(s);
  }
  display.write("\n");

  // 2nd line
  //display.setCursor(0, 8);     
  display.write("Boi:");
  sprintf(s, "%.1f",boiler_temp);
  display.write( s );
  display.write("C ");

  // 3rd line
  display.write("Sol:");
  sprintf(s, "%.1f",solar_temp);
  display.write(s);
  display.write("C\n");

  // 4rd line
  display.write("Wifi ");
  display.write(WiFi.SSID().c_str());
  display.write(": ");

  if (!q_boiler && !q_solar && !button_state && menue_select == 0 && wificheck == 0)
  {
    check_status();
    wificheck = 30;
  }
      if (!initialConfig)
          {
            uint8_t status;
        
            if(wificheck > 0)
            {
              wificheck--;
            }

              status = WiFi.status();

              if (status == WL_CONNECTED )
              {
                wificheck = 0;
                display.write("good");
              }
              else
                display.write("bad");

            

            if ( status == WL_CONNECTED )
            {
              LOGERROR1(F("WiFi connected after time: "), 20-wificheck);
              LOGERROR0(F("WiFi connected "));
              LOGERROR3(F("SSID:"), WiFi.SSID(), F(",RSSI="), WiFi.RSSI());
              LOGERROR3(F("Channel:"), WiFi.channel(), F(",IP address:"), WiFi.localIP() );
            }
            else
            {
              LOGERROR1(F("WiFi not connected "), wificheck);
            }

            

          }
          else
              display.write("no conf");
  

  }
  
  display.display();






}


