#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>
#include <EEPROM.h>
#include "graphics.h"
#include "fonts.h"
#include <Wire.h>
#include <DS3231.h>
#include "Adafruit_Si7021.h"
#include "WiFi.h"
#include "esp_wps.h"
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

// ###############################################
// ### OVER THE AIR - Firmware update settings ###
// ###############################################
const char* OTA_CurrentDate     = "16/12/2022";
const float OTA_CurrentVersion  = 2.37;
const char* OTA_UpdateURL       = "http://192.168.1.6/ESP8266_OTA/";
const char* OTA_SystemName      = "MQTT-THERMOSTAT";
const long  OTA_UpdatesInterval = 600;
// ###############################################

// MQTT settings
const char* MQTT_BaseTopic = "/domotica/thermostat/";

// EEPROM VALUES MAP
#define EEPROM_ADDR_THERMO_MODE          0
#define EEPROM_ADDR_THERMO_T0            1
#define EEPROM_ADDR_THERMO_T1            3
#define EEPROM_ADDR_THERMO_T2            5
#define EEPROM_ADDR_THERMO_TM            7
#define EEPROM_ADDR_MQTT_IP_A            9
#define EEPROM_ADDR_MQTT_IP_B            10
#define EEPROM_ADDR_MQTT_IP_C            11
#define EEPROM_ADDR_MQTT_IP_D            12
#define EEPROM_ADDR_TEMP_GAIN            13
#define EEPROM_ADDR_HUMI_GAIN            14
#define EEPROM_NEXTFREE_ADDR             15
#define EEPROM_ADDR_THERMO_SCHEDULE      30

// General defines
#define DISPLAY_RefreshRate        0.5f               // Update the display every second by default if no other events occurs (in seconds)
#define DISPLAY_BackToHomeTimeout  20                 // Time in seconds of inactivity after which return to home screen from other screens (in seconds)
#define ENVIRONMENT_ReadInterval   30                 // Temperature/Humidity read interval (in seconds): MUST BE >= 10
#define TOUCH_DownKeptDelay        0.7f               // Delay when touch is kept pressed before start repeat action automatically (in seconds)
#define TOUCH_DownKeptDelayRepeat  0.15f              // Delay between each repeated action when touch is kept pressed (in seconds)
#define CALIBRATION_FILE           "/TouchCalData3"   // File containing touch calibration data
#define EEPROM_SIZE                512
#define TEMP_HYSTERESIS_LO         0.3f               // Hysteresis on lowering temperature before turn off rele
#define TEMP_HYSTERESIS_HI         0.1f               // Hysteresis on rising temperature before turn on rele
#define TEMP_OFF_MIN               7.0f               // Minumim temperature set-point for OFF/ANTI-ICE
#define TEMP_OFF_MAX               11.0f              // Maximum temperature set-point for OFF/ANTI-ICE
#define TEMP_ON_MIN                15.0f              // Minumim temperature set-point for MANUAL/AUTO/HOLIDAY
#define TEMP_ON_MAX                30.0f              // Maximum temperature set-point for MANUAL/AUTO/HOLIDAY
#define TEMP_GAIN_MIN              50                 // Minimum temperature-gain value for calculations
#define TEMP_GAIN_MAX              150                // Maximum temperature-gain value for calculations
#define HUMI_GAIN_MIN              25                 // Minimum humidity-gain value for calculations
#define HUMI_GAIN_MAX              200                // Maximum humidity-gain value for calculations
#define WIFI_RetryAttempts_TIMEOUT 15
#define MQTT_RetryAttempts_TIMEOUT 15
#define MQTT_KeepAlive_TIMEOUT     3600               // Time in seconds a clinet connection is keptconnected if no messages are exchanged (server caluclate time * 1.5)
//#define TEMPLATE_COLOR_CLOCK       tft.color565(41,152,255)
//#define TEMPLATE_COLOR_CLOCK       tft.color565(41,152,255)

// Define ESP32 GPIO lines used
#define TFT_MISO                 19
#define TFT_MOSI                 23
#define TFT_SCLK                 18
#define TFT_CS                   5
#define TFT_DC                   4
#define TFT_RST                  22
#define TFT_BL                   15
#define TOUCH_CS                 14
#define TOUCH_IRQ                27
#define BEEPER                   21
#define I2C_Line_SCL             33
#define I2C_Line_SDA             26
#define RELE_Output              32

// Define Si7021 temp/humidity sensor object
Adafruit_Si7021 si7021 = Adafruit_Si7021();

// Define SPI objects
TFT_eSPI tft = TFT_eSPI(240,320);            // Invoke custom library (width: 240 - height: 320)

// Define I2C device objects
DS3231 Clock;

// WIFI connection settings
byte MacAddress[6];
String ESP_Hostname = " ";

// MQTT program data
IPAddress MQTT_Server(0, 0, 0, 0);
IPAddress MQTT_Server_PREV(0, 0, 0, 0);
String MQTT_Topic = " ";
WiFiClient espClient;
PubSubClient mqttClient(espClient);
byte MQTT_RetryAttempts = 0;


// Define the structure for WPS connection
static esp_wps_config_t WIFIconfig;

// Global program variables
char TIME[6] = "00:00";
char TIME_PREV[6] = "00:00";
byte TIME_HOUR = 0;
char DATE[11] = "00/00/0000";
char DATE_PREV[11] = "00/00/0000";
char DAY[4] = "DOM";
char DAY_PREV[4] = "---";
byte DAY_NUM = 0;
float TEMPERATURE = 0.0f;
float TEMPERATURE_PREV = -99.9f;
byte HUMIDITY = 0;
byte HUMIDITY_PREV = 200;
byte SI7021_DATAVALID = 0;
byte SI7021_DATAVALID_PREV = 200;
byte SI7021_PRESENT = 0;
byte TEMP_GAIN = 100; // To be divided by 100 for calculations
byte TEMP_GAIN_PREV = 0;
byte HUMI_GAIN = 100; // To be divided by 100 for calculations
byte HUMI_GAIN_PREV = 0;
float SETPOINT = 21.5f;
float SETPOINT_PREV = -99.9f;
byte RELE_Status = 0;
byte RELE_Status_PREV = -1;
byte displayUpdate = 1;
byte forceFirmwareUpdate = 0;
byte forceWPSpairing = 0;
unsigned long lastRefresh = 0;
unsigned long lastActivity = 0;
unsigned long lastEnvRead = 0;
unsigned long lastFirmwareCheck =0;
byte MENU_Section = 0;
byte MENU_Section_PREV = 255;
byte TOUCH_Down = 0;
unsigned long TOUCH_DownStart = 0;
byte SETTINGS_PositionX = 0;
byte SETTINGS_PositionX_PREV = 255;
byte SETTINGS_PositionY = 0;
byte SETTINGS_PositionY_PREV = 255;
String WIFI_SSID = "";
byte WIFI_Signal = 0;
byte UPDT_Status = 0;
byte MQTT_Status = 0;
String WIFI_SSID_PREV = "-";
byte WIFI_Signal_PREV = 99;
byte UPDT_Status_PREV = 99;
byte MQTT_Status_PREV = 99;
byte WIFI_RetryAttempts = 0;

unsigned char DEF_WEEK [] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2};
unsigned char DEF_WEEKEND [] = {1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};

unsigned char THERMO_Schedule_1 [] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2};
unsigned char THERMO_Schedule_2 [] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2};
unsigned char THERMO_Schedule_3 [] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2};
unsigned char THERMO_Schedule_4 [] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2};
unsigned char THERMO_Schedule_5 [] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2};
unsigned char THERMO_Schedule_6 [] = {1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
unsigned char THERMO_Schedule_7 [] = {1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
unsigned char THERMO_Schedule_H [] = {1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
byte THERMO_Schedule_CHANGE = 0;
byte THERMO_Mode = 0;            // 0-Off | 1-Manual | 2-Auto | 3-Holiday
byte THERMO_Mode_PREV = 255;
float THERMO_T0 = 9.0f;
float THERMO_T1 = 19.5f;
float THERMO_T2 = 22.0f;
float THERMO_TM = 23.0f;
float THERMO_T0_PREV = -39.9f;
float THERMO_T1_PREV = -39.9f;
float THERMO_T2_PREV = -39.9f;
float THERMO_TM_PREV = -39.9f;

///////////////////////////////////////////////////////////////////////////////////////////////
TaskHandle_t NetMQTT_Task;
///////////////////////////////////////////////////////////////////////////////////////////////


//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
char* string2char(String command){
    if(command.length()!=0){
        char *p = const_cast<char*>(command.c_str());
        return p;
    }
}
//---------------------------------------------------------------------------------------------------------
void SOUND_Startup() {
   // Emit a BEEP at startup
   ledcWriteTone(0,1500);
   delay(50);
   ledcWriteTone(0,2000);
   delay(50);
   ledcWriteTone(0,2500);
   delay(50);
   ledcWriteTone(0,0);
}
//---------------------------------------------------------------------------------------------------------
void SOUND_KeyPress() {
   // Emit a BEEP at startup
   ledcWriteTone(0,1500);
   delay(25);
   ledcWriteTone(0,0);
}
//---------------------------------------------------------------------------------------------------------
void SOUND_ButtonActivate() {
   // Emit a BEEP at startup
   ledcWriteTone(0,4000);
   delay(50);
   ledcWriteTone(0,0);
}
//---------------------------------------------------------------------------------------------------------
void RTC_ReadClock() {
   bool h12, PM, Century=false;
   snprintf(DATE, sizeof(DATE), "%02u/%02u/20%02u", Clock.getDate(), Clock.getMonth(Century), Clock.getYear() );
   snprintf(TIME, sizeof(TIME), "%02u:%02u", Clock.getHour(h12, PM), Clock.getMinute() );
   TIME_HOUR = Clock.getHour(h12, PM);
   DAY_NUM = Clock.getDoW();
   switch (DAY_NUM) {
      case 2:  snprintf(DAY, sizeof(DAY), "LUN");   break;
      case 3:  snprintf(DAY, sizeof(DAY), "MAR");   break;
      case 4:  snprintf(DAY, sizeof(DAY), "MER");   break;
      case 5:  snprintf(DAY, sizeof(DAY), "GIO");   break;
      case 6:  snprintf(DAY, sizeof(DAY), "VEN");   break;
      case 7:  snprintf(DAY, sizeof(DAY), "SAB");   break;
      default: snprintf(DAY, sizeof(DAY), "DOM");   break;
   }
}
//---------------------------------------------------------------------------------------------------------
void AMBIENT_ReadData() {
   float siTEMPERATURE = 0.0f;
   float siHUMIDITY = 0.0f;
   byte siDATAVALID = 1;
   // Read SI7021 temperature and humidity (if sensor is present)
   if (SI7021_PRESENT == 1) {
      siTEMPERATURE = si7021.readTemperature();
      siHUMIDITY = si7021.readHumidity();
      if (isnan(siTEMPERATURE)) { siTEMPERATURE = 0; siDATAVALID = 0; } else { siTEMPERATURE *= (float(TEMP_GAIN)/100.0f); }
      if (isnan(siHUMIDITY)) { siHUMIDITY = 0; } else { siHUMIDITY *= (float(HUMI_GAIN)/100.0f); }
      if (siHUMIDITY < 0) { siHUMIDITY = 0; }
      if (siHUMIDITY > 100) { siHUMIDITY = 100; }
      //si7021.heater(true|false); <-- enable sensor heater 30 seconds every minute
      //si7021.reset();
   } else {
      // SI7021 not present, store dummy data
      siTEMPERATURE = 0;
      siHUMIDITY = 0;
      siDATAVALID = 0;
   }
   // Store global temperature/humidity for thermostat calculations
   SI7021_DATAVALID = siDATAVALID;
   HUMIDITY = byte(siHUMIDITY);
   TEMPERATURE = siTEMPERATURE;
}
//---------------------------------------------------------------------------------------------------------
void touch_calibrate() {
   uint16_t calData[5];
   uint8_t calDataOK = 0;
   // Calibrate
   tft.fillScreen(TFT_BLACK);
   tft.setCursor(20, 0);
   tft.setTextFont(2);
   tft.setTextSize(1);
   tft.setTextColor(TFT_WHITE, TFT_BLACK);
   tft.println("Touch corners as indicated");
   tft.setTextFont(1);
   tft.println();
   tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);
   Serial.println();
   Serial.println("// Use this calibration code in setup():");
   Serial.print("  uint16_t calData[5] = ");
   Serial.print("{ ");
   for (uint8_t i = 0; i < 5; i++) {
      Serial.print(calData[i]);
      if (i < 4) Serial.print(", ");
   }
   Serial.println(" };");
   Serial.print("  tft.setTouch(calData);");
   Serial.println();
   Serial.flush();
   tft.fillScreen(TFT_BLACK);
   tft.setTextColor(TFT_GREEN, TFT_BLACK);
   tft.println("Calibration complete!");
   tft.println("Calibration code sent to Serial port.");
   delay(4000);
}
//---------------------------------------------------------------------------------------------------------
float EEPROM_ReadFloat(int epADDR) {
   byte VL1, VL2;
   int tempVL;
   // Read the float value
   VL1 = EEPROM.read(epADDR+0);
   VL2 = EEPROM.read(epADDR+1);
   tempVL = (int)(VL2 << 8) | VL1;
   return (float(tempVL) / 10.0f);
}
//---------------------------------------------------------------------------------------------------------
void EEPROM_WriteFloat(int epADDR, float myVAL) {
   int tempVL = int(myVAL * 10.0f);
   EEPROM.write(epADDR+0, byte(tempVL & 0xff));
   EEPROM.write(epADDR+1, byte((tempVL >> 8) & 0xff));
}
//---------------------------------------------------------------------------------------------------------
void EEPROM_ReadSettings() {
   byte iii;
   Serial.println("EEPROM: reading settings...");
   Serial.flush();
   // Read thermostat data from EEPROM
   THERMO_Mode = EEPROM.read(EEPROM_ADDR_THERMO_MODE);
   THERMO_T0 = EEPROM_ReadFloat(EEPROM_ADDR_THERMO_T0);
   THERMO_T1 = EEPROM_ReadFloat(EEPROM_ADDR_THERMO_T1);
   THERMO_T2 = EEPROM_ReadFloat(EEPROM_ADDR_THERMO_T2);
   THERMO_TM = EEPROM_ReadFloat(EEPROM_ADDR_THERMO_TM);
   THERMO_Mode_PREV = THERMO_Mode;
   THERMO_T0_PREV = THERMO_T0;
   THERMO_T1_PREV = THERMO_T1;
   THERMO_T2_PREV = THERMO_T2;
   THERMO_TM_PREV = THERMO_TM;
   // Read MQTT server address from EEPROM
   MQTT_Server[0] = EEPROM.read(EEPROM_ADDR_MQTT_IP_A);
   MQTT_Server[1] = EEPROM.read(EEPROM_ADDR_MQTT_IP_B);
   MQTT_Server[2] = EEPROM.read(EEPROM_ADDR_MQTT_IP_C);
   MQTT_Server[3] = EEPROM.read(EEPROM_ADDR_MQTT_IP_D);
   MQTT_Server_PREV[0] = MQTT_Server[0];
   MQTT_Server_PREV[1] = MQTT_Server[1];
   MQTT_Server_PREV[2] = MQTT_Server[2];
   MQTT_Server_PREV[3] = MQTT_Server[3];
   // Read the TEMPERATURE GAIN value
   TEMP_GAIN = EEPROM.read(EEPROM_ADDR_TEMP_GAIN);
   TEMP_GAIN_PREV = TEMP_GAIN;
   // Read the HUMIDITY GAIN value
   HUMI_GAIN = EEPROM.read(EEPROM_ADDR_HUMI_GAIN);
   HUMI_GAIN_PREV = HUMI_GAIN;
   // Read thermostat schedule from EEPROM
   for (iii=0; iii<24; iii++) {
      THERMO_Schedule_1[iii] = EEPROM.read(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+0);
      THERMO_Schedule_2[iii] = EEPROM.read(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+1);
      THERMO_Schedule_3[iii] = EEPROM.read(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+2);
      THERMO_Schedule_4[iii] = EEPROM.read(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+3);
      THERMO_Schedule_5[iii] = EEPROM.read(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+4);
      THERMO_Schedule_6[iii] = EEPROM.read(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+5);
      THERMO_Schedule_7[iii] = EEPROM.read(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+6);
      THERMO_Schedule_H[iii] = EEPROM.read(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+7);
      if ((THERMO_Schedule_1[iii] < 1)||(THERMO_Schedule_1[iii] > 2)) { THERMO_Schedule_1[iii] = DEF_WEEK[iii]; THERMO_Schedule_CHANGE = 1; }
      if ((THERMO_Schedule_2[iii] < 1)||(THERMO_Schedule_2[iii] > 2)) { THERMO_Schedule_2[iii] = DEF_WEEK[iii]; THERMO_Schedule_CHANGE = 1; }
      if ((THERMO_Schedule_3[iii] < 1)||(THERMO_Schedule_3[iii] > 2)) { THERMO_Schedule_3[iii] = DEF_WEEK[iii]; THERMO_Schedule_CHANGE = 1; }
      if ((THERMO_Schedule_4[iii] < 1)||(THERMO_Schedule_4[iii] > 2)) { THERMO_Schedule_4[iii] = DEF_WEEK[iii]; THERMO_Schedule_CHANGE = 1; }
      if ((THERMO_Schedule_5[iii] < 1)||(THERMO_Schedule_5[iii] > 2)) { THERMO_Schedule_5[iii] = DEF_WEEK[iii]; THERMO_Schedule_CHANGE = 1; }
      if ((THERMO_Schedule_6[iii] < 1)||(THERMO_Schedule_6[iii] > 2)) { THERMO_Schedule_6[iii] = DEF_WEEKEND[iii]; THERMO_Schedule_CHANGE = 1; }
      if ((THERMO_Schedule_7[iii] < 1)||(THERMO_Schedule_7[iii] > 2)) { THERMO_Schedule_7[iii] = DEF_WEEKEND[iii]; THERMO_Schedule_CHANGE = 1; }
      if ((THERMO_Schedule_H[iii] < 1)||(THERMO_Schedule_H[iii] > 2)) { THERMO_Schedule_H[iii] = DEF_WEEKEND[iii]; THERMO_Schedule_CHANGE = 1; }
   }
   if ((THERMO_Mode>3)||(THERMO_T0<TEMP_OFF_MIN)||(THERMO_T0>TEMP_OFF_MAX)||(THERMO_T1<TEMP_ON_MIN)||(THERMO_T1>TEMP_ON_MAX)||(THERMO_T2<TEMP_ON_MIN)||(THERMO_T2>TEMP_ON_MAX)||(THERMO_TM<TEMP_ON_MIN)||(THERMO_TM>TEMP_ON_MAX)||(TEMP_GAIN<TEMP_GAIN_MIN)||(TEMP_GAIN>TEMP_GAIN_MAX)||(HUMI_GAIN<HUMI_GAIN_MIN)||(HUMI_GAIN>HUMI_GAIN_MAX)) {
      // EEprom data is invalid, write default config
      Serial.println("EEPROM: invalid contents, initializing to defaults...");
      Serial.flush();
      THERMO_Mode = 0;    // OFF
      THERMO_T0 = 9.0f;   // ANTI-ICE: 9.0^C
      THERMO_T1 = 19.5f;  // T1: 19.5^C
      THERMO_T2 = 22.0f;  // T2: 22.0^C
      THERMO_TM = 23.0f;  // TM: 23.0^C
      THERMO_Mode_PREV = 255;    // OFF
      THERMO_T0_PREV = -99.9f;   // ANTI-ICE: 9.0^C
      THERMO_T1_PREV = -99.9f;  // T1: 19.5^C
      THERMO_T2_PREV = -99.9f;  // T2: 22.0^C
      THERMO_TM_PREV = -99.9f;  // T2: 22.0^C
      MQTT_Server[0] = 192;
      MQTT_Server[1] = 168;
      MQTT_Server[2] = 1;
      MQTT_Server[3] = 6;
      TEMP_GAIN = 100;
      HUMI_GAIN = 100;
      for (iii=0; iii<24; iii++) {
         THERMO_Schedule_1[iii] = DEF_WEEK[iii];
         THERMO_Schedule_2[iii] = DEF_WEEK[iii];
         THERMO_Schedule_3[iii] = DEF_WEEK[iii];
         THERMO_Schedule_4[iii] = DEF_WEEK[iii];
         THERMO_Schedule_5[iii] = DEF_WEEK[iii];
         THERMO_Schedule_6[iii] = DEF_WEEKEND[iii];
         THERMO_Schedule_7[iii] = DEF_WEEKEND[iii];
         THERMO_Schedule_H[iii] = DEF_WEEKEND[iii];
      }
      THERMO_Schedule_CHANGE = 1;
   }
   // Write settings if something was read wrong/default configured
   EEPROM_WriteSettings();
}
//---------------------------------------------------------------------------------------------------------
void EEPROM_WriteSettings() {
   byte iii;
   byte flgSAVE = 0;
   // Store THERMO_MODE value if changed
   if (THERMO_Mode!=THERMO_Mode_PREV) { EEPROM.write(EEPROM_ADDR_THERMO_MODE, THERMO_Mode); flgSAVE=1; Serial.println("EEPROM: mode changed."); }
   // Store T0 temperature if changed
   if (THERMO_T0!=THERMO_T0_PREV) { EEPROM_WriteFloat(EEPROM_ADDR_THERMO_T0, THERMO_T0); flgSAVE=1; Serial.println("EEPROM: T0 changed."); }
   // Store T1 temperature if changed
   if (THERMO_T1!=THERMO_T1_PREV) { EEPROM_WriteFloat(EEPROM_ADDR_THERMO_T1, THERMO_T1); flgSAVE=1; Serial.println("EEPROM: T1 changed."); }
   // Store T2 temperature if changed
   if (THERMO_T2!=THERMO_T2_PREV) { EEPROM_WriteFloat(EEPROM_ADDR_THERMO_T2, THERMO_T2); flgSAVE=1; Serial.println("EEPROM: T2 changed."); }
   // Store TM temperature if changed
   if (THERMO_TM!=THERMO_TM_PREV) { EEPROM_WriteFloat(EEPROM_ADDR_THERMO_TM, THERMO_TM); flgSAVE=1; Serial.println("EEPROM: TM changed."); }
   // Store MQTT Server Address
   if (MQTT_Server[0]!=MQTT_Server_PREV[0]) { EEPROM.write(EEPROM_ADDR_MQTT_IP_A, MQTT_Server[0]); flgSAVE=1; Serial.println("EEPROM: MQTT byte 1 changed."); }
   if (MQTT_Server[1]!=MQTT_Server_PREV[1]) { EEPROM.write(EEPROM_ADDR_MQTT_IP_B, MQTT_Server[1]); flgSAVE=1; Serial.println("EEPROM: MQTT byte 2 changed."); }
   if (MQTT_Server[2]!=MQTT_Server_PREV[2]) { EEPROM.write(EEPROM_ADDR_MQTT_IP_C, MQTT_Server[2]); flgSAVE=1; Serial.println("EEPROM: MQTT byte 3 changed."); }
   if (MQTT_Server[3]!=MQTT_Server_PREV[3]) { EEPROM.write(EEPROM_ADDR_MQTT_IP_D, MQTT_Server[3]); flgSAVE=1; Serial.println("EEPROM: MQTT byte 4 changed."); }
   // Store TEMPERATURE GAIN value if changed
   if (TEMP_GAIN!=TEMP_GAIN_PREV) { EEPROM.write(EEPROM_ADDR_TEMP_GAIN, TEMP_GAIN); flgSAVE=1; Serial.println("EEPROM: T.GAIN changed."); }
   // Store HUMIDITY GAIN value if changed
   if (HUMI_GAIN!=HUMI_GAIN_PREV) { EEPROM.write(EEPROM_ADDR_HUMI_GAIN, HUMI_GAIN); flgSAVE=1; Serial.println("EEPROM: H.GAIN changed."); }
   // Store SCHEDULE
   if (THERMO_Schedule_CHANGE==1) {
      Serial.println("EEPROM: schedule changed."); 
      for (iii=0; iii<24; iii++) {
         EEPROM.write(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+0, THERMO_Schedule_1[iii]);
         EEPROM.write(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+1, THERMO_Schedule_2[iii]);
         EEPROM.write(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+2, THERMO_Schedule_3[iii]);
         EEPROM.write(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+3, THERMO_Schedule_4[iii]);
         EEPROM.write(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+4, THERMO_Schedule_5[iii]);
         EEPROM.write(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+5, THERMO_Schedule_6[iii]);
         EEPROM.write(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+6, THERMO_Schedule_7[iii]);
         EEPROM.write(EEPROM_ADDR_THERMO_SCHEDULE+(iii*10)+7, THERMO_Schedule_H[iii]);
      }
      THERMO_Schedule_CHANGE = 0;
      flgSAVE=1;
   }
   // Commit data if something changed
   if (flgSAVE==1) { 
      Serial.println("EEPROM: writing settings...");
      Serial.flush();
      EEPROM.commit();
   }
}
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
String wpspin2string(uint8_t a[]){
  char wps_pin[9];
  for(int i=0;i<8;i++){
    wps_pin[i] = a[i];
  }
  wps_pin[8] = '\0';
  return (String)wps_pin;
}
//---------------------------------------------------------------------------------------------------------
void wpsInitConfig(){
   char *NEW_Hostname = const_cast<char*>(ESP_Hostname.c_str());
   //WIFIconfig.crypto_funcs = &g_wifi_default_wps_crypto_funcs;
   WIFIconfig.wps_type = WPS_TYPE_PBC;   // WPS_TYPE_PIN --> Use PIN access code
   strcpy(WIFIconfig.factory_info.manufacturer, NEW_Hostname);
   strcpy(WIFIconfig.factory_info.model_number, NEW_Hostname);
   strcpy(WIFIconfig.factory_info.model_name, NEW_Hostname);
   strcpy(WIFIconfig.factory_info.device_name, NEW_Hostname);
}
//---------------------------------------------------------------------------------------------------------
void WiFiEvent(WiFiEvent_t event, arduino_event_info_t info){   
   const char * NEW_Hostname = ESP_Hostname.c_str();
   switch(event){
      case ARDUINO_EVENT_WIFI_STA_START:
         // Networking started
         displayUpdate = 1;
         break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
         // Networking connected to valid network and got valid IP
         forceWPSpairing = 0;
         displayUpdate = 1;
         break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
         // Networking disconnected, attempting reconnect
         displayUpdate = 1;
         WiFi.reconnect();
         break;
      case ARDUINO_EVENT_WPS_ER_SUCCESS:
         // WPS registration success, connecting to registered SSID
         forceWPSpairing = 2;
         displayUpdate = 1;
         esp_wifi_wps_disable();
         delay(10);
         WiFi.begin();
         WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
         WiFi.setHostname(NEW_Hostname);
         break;
      case ARDUINO_EVENT_WPS_ER_FAILED:
         // WPS Failed, retrying
         forceWPSpairing = 3;
         displayUpdate = 1;
         esp_wifi_wps_disable();
         esp_wifi_wps_enable(&WIFIconfig);
         esp_wifi_wps_start(0);
         break;
      case ARDUINO_EVENT_WPS_ER_TIMEOUT:
         // WPS Timedout, retrying
         forceWPSpairing = 4;
         displayUpdate = 1;
         esp_wifi_wps_disable();
         esp_wifi_wps_enable(&WIFIconfig);
         esp_wifi_wps_start(0);
         break;
      case ARDUINO_EVENT_WPS_ER_PIN:
         // WPS PIN to display  -->  String myPIN = wpspin2string(info.sta_er_pin.pin_code);
         break;
      default:
         break;
  }
}
//---------------------------------------------------------------------------------------------------------
void WPS_Start() {
   if (WiFi.status() != WL_CONNECTED) {
      WiFi.onEvent(WiFiEvent);
      WiFi.mode(WIFI_MODE_STA);
      Serial.println("Starting WPS");
      wpsInitConfig();
      esp_wifi_wps_enable(&WIFIconfig);
      esp_wifi_wps_start(0);
   } else {
      Serial.println();
      Serial.print("Connesso a: ");
      Serial.println(WiFi.SSID());
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
   }
}
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
void MQTT_Reconnect() {
   // Loop until we're reconnected
   Serial.print("MQTT: connecting to server... ");
   // Attempt to connect
   mqttClient.setKeepAlive(MQTT_KeepAlive_TIMEOUT);
   if (mqttClient.connect(MQTT_Topic.c_str())) {
      // Connection success, subscribe to a topic
      Serial.println("Success");
      mqttClient.subscribe(String(MQTT_Topic + "/set/#").c_str());
   } else {
      Serial.print("Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 10 seconds");
   }
}
//---------------------------------------------------------------------------------------------------------
void MQTT_CallBack(char* topic, byte* payload, unsigned int length) {
   String recvMSG = "-";
   int flgSENT = 0;
   // Store the message received from MQTT
   for (int i=0;i<length;i++) {
      char receivedChar = (char)payload[i];
      if (i==0) {
         recvMSG = String(receivedChar);
      } else {
         recvMSG += String(receivedChar);
      }
   }
   // Analize the command received
   char msg[50];
   snprintf (msg, sizeof(msg), "NAK");
   byte isVALID = 0;
   float paramVL = -99.9;
   Serial.print("MQTT: topic '"); Serial.print(topic); Serial.println("' with message '");
   Serial.print(recvMSG);
   Serial.print("' is ");
   // ####################################
   // ### CONFIGURATION WRITE COMMANDS ###
   // ####################################
   if (strcmp(topic, "setpoint") != 0) {
      float n = sscanf(string2char(recvMSG), "%f", &paramVL);
      if ((THERMO_Mode==0)&&(paramVL>=TEMP_OFF_MIN)&&(paramVL<=TEMP_OFF_MAX)) { isVALID = 1; THERMO_T0 = paramVL; displayUpdate = 1; }
      if ((THERMO_Mode==1)&&(paramVL>=TEMP_ON_MIN)&&(paramVL<=TEMP_ON_MAX)) { isVALID = 1; THERMO_TM = paramVL; displayUpdate = 1; }
      if ((THERMO_Mode==2)&&(paramVL>=TEMP_ON_MIN)&&(paramVL<=TEMP_ON_MAX)) { isVALID = 1; THERMO_T2 = paramVL; displayUpdate = 1; }
      if ((THERMO_Mode==3)&&(paramVL>=TEMP_ON_MIN)&&(paramVL<=TEMP_ON_MAX)) { isVALID = 1; THERMO_T2 = paramVL; displayUpdate = 1; }
   }
   if (strcmp(topic, "mode") != 0) {
      if (recvMSG.indexOf("off") >= 0) { isVALID = 1; THERMO_Mode = 0; displayUpdate = 1; }
      if (recvMSG.indexOf("heat") >= 0) { isVALID = 1; THERMO_Mode = 1; displayUpdate = 1; }
      if (recvMSG.indexOf("auto") >= 0) { isVALID = 1; THERMO_Mode = 2; displayUpdate = 1; }
      if (recvMSG.indexOf("holiday") >= 0) { isVALID = 1; THERMO_Mode = 3; displayUpdate = 1; }
   }
   // ############################
   // ### STATUS READ COMMANDS ###
   // ############################
   if (strcmp(topic, "getdata") != 0) { isVALID = 1; displayUpdate = 1; }
   // ### Output on MQTT topic and console the status of the requested command ###
   if (isVALID == 1) { Serial.println("valid."); } else { Serial.println("invalid."); }
}
//---------------------------------------------------------------------------------------------------------
void MQTT_SendStatus() {
   char msg[50];
   // Send CURRENT TEMPERATURE
   snprintf (msg, sizeof(msg), "%0.1f", TEMPERATURE);
   mqttClient.publish((MQTT_Topic+"/temp").c_str(), msg);
   yield();
   // Send CURRENT HUMIDITY
   snprintf (msg, sizeof(msg), "%d", HUMIDITY);
   mqttClient.publish((MQTT_Topic+"/humidity").c_str(), msg);
   yield();
   // Send CURRENT MODE
   switch (THERMO_Mode) {
      case 1:  snprintf (msg, sizeof(msg), "heat"); break;
      case 2:  snprintf (msg, sizeof(msg), "auto"); break;
      case 3:  snprintf (msg, sizeof(msg), "holiday"); break;
      default: snprintf (msg, sizeof(msg), "off"); break;
   }
   mqttClient.publish((MQTT_Topic+"/mode").c_str(), msg);
   yield();
   // Send CURRENT STATUS
   snprintf (msg, sizeof(msg), "off");
   if (RELE_Status==1) { snprintf (msg, sizeof(msg), "heating"); }
   if (RELE_Status==0 && THERMO_Mode!=0) { snprintf (msg, sizeof(msg), "idle"); }
   mqttClient.publish((MQTT_Topic+"/status").c_str(), msg);
   yield();
   // Send OFF TEMPERATURE
   snprintf (msg, sizeof(msg), "%0.1f", THERMO_T0);
   mqttClient.publish((MQTT_Topic+"/tempoff").c_str(), msg);
   yield();
   // Send AUTO TEMPERATURE
   snprintf (msg, sizeof(msg), "%0.1f", THERMO_T2);
   mqttClient.publish((MQTT_Topic+"/tempauto").c_str(), msg);
   yield();
   // Send MANUAL TEMPERATURE
   snprintf (msg, sizeof(msg), "%0.1f", THERMO_TM);
   mqttClient.publish((MQTT_Topic+"/tempman").c_str(), msg);
   yield();
   // Send CURRENT SETPOINT
   snprintf (msg, sizeof(msg), "%0.1f", SETPOINT);
   mqttClient.publish((MQTT_Topic+"/setpoint").c_str(), msg);
   yield();
}
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------
void SCREEN_UpdatePopupOpen(float newVer) {
   char stMSG[60];
   // Display the popup area on screen with title on screen
   tft.drawRect(59,69, 202,102, TFT_BLACK);
   tft.drawRect(60,70, 200,100, TFT_WHITE);
   tft.fillRect(61,71, 198,98, tft.color565(160,160,160));
   tft.drawRect(60,70, 200,20, TFT_WHITE);
   tft.fillRect(61,71, 198,18, tft.color565(255,128,0));
   // Draw the popup title
   tft.setFreeFont(FSSB12);
   tft.setTextColor(TFT_BLACK);
   tft.drawString("Firmware Update", 110,71, 2);
   // Draw the actual/new version update message
   snprintf(stMSG, sizeof(stMSG), "Upgrading from %.2f to %.02f", OTA_CurrentVersion, newVer);
   tft.setTextColor(TFT_WHITE);
   tft.drawString(stMSG, 65,101, 2);
   // Draw the progress bar border and value
   tft.drawRect(65,135, 188,20, TFT_WHITE);
   tft.fillRect(66,136, 186,18, tft.color565(160,160,160));
   tft.setTextColor(TFT_WHITE);
   tft.drawCentreString("0%", 159,137, 2);
   // Force a complete screen refresh at end of operations
   displayUpdate = 2;
}
//--------------------------------------------------------------------------------------------------------
void SCREEN_UpdatePopupProgress(int curr, int total) {
   char stMSG[10];
   static int prev_PRCT = 0;
   // Calculate percentual of work
   int PRCT = int(curr * 100 / total);
   // Update progress only if changed from previous one
   if (prev_PRCT != PRCT) {
      // Update the percentage bar
      int boxPRCT = int(curr * 186 / total);
      // Draw the progress bar border and value
      tft.fillRect(66,136, 186,18, tft.color565(160,160,160));
      tft.fillRect(66,136, boxPRCT,18, tft.color565(255,112,160));
      // Update the percentage label
      snprintf(stMSG, sizeof(stMSG), "%i%%", PRCT);
      tft.setTextColor(TFT_WHITE);
      tft.drawCentreString(stMSG, 159,137, 2);
      // Store updated value for next processing
      prev_PRCT = PRCT;
   }
}
//--------------------------------------------------------------------------------------------------------
void SCREEN_UpdatePopupFinished() {
   tft.fillRect(65,135, 188,20, TFT_DARKGREEN);
   tft.setTextColor(TFT_WHITE);
   tft.drawCentreString("DONE", 159,137, 2);
   Serial.println("RUN: firmware update success, restarting...");
   Serial.flush();
}
//--------------------------------------------------------------------------------------------------------
void SCREEN_UpdatePopupError(int err) {
   char stMSG[30];
   tft.fillRect(65,135, 188,20, TFT_RED);
   tft.setTextColor(TFT_WHITE);
   snprintf(stMSG, sizeof(stMSG), "UPDATE FAILED (%d)", err);
   tft.drawCentreString(stMSG, 159,137, 2);
}
//--------------------------------------------------------------------------------------------------------
void SCREEN_UpdatePopupUnavailable() {
   tft.fillRect(65,135, 188,20, TFT_YELLOW);
   tft.setTextColor(TFT_WHITE);
   tft.drawCentreString("NOT AVAILABLE", 159,137, 2);
}
//--------------------------------------------------------------------------------------------------------
void checkFirmwareUpdates() {
   String fwVersionURL = String(OTA_UpdateURL);
   int Cx;
   char stMSG[20];
   WiFiClient client;
   // Build the update url
   fwVersionURL.concat(OTA_SystemName);
   fwVersionURL.concat(".version");
   Serial.print("FIRMWARE: checking for firmware updates("); Serial.print(fwVersionURL); Serial.println(")");
   snprintf(stMSG, sizeof(stMSG), "%.02f", OTA_CurrentVersion);
   Serial.print("FIRMWARE: current version is "); Serial.println(stMSG);
   // Activate UPDATE icon on display
   tft.setFreeFont(FSSB12);
   tft.fillRect(187,4, 40,17, tft.color565(255,128,0));
   tft.setTextColor(TFT_WHITE);
   tft.drawString("UPDT", 192,4, 2);
   // Download the version firmware file
   HTTPClient httpClient;
   httpClient.begin( fwVersionURL );
   int httpCode = httpClient.GET();
   if ( httpCode == 200 ) {
      // Version firmware file download success
      String newFWVersion = httpClient.getString();
      float newVersion = newFWVersion.toFloat();
      snprintf(stMSG, sizeof(stMSG), "%.02f", newVersion );
      if( newVersion > OTA_CurrentVersion ) {
         // Display the Update popup on screen
         SCREEN_UpdatePopupOpen(newVersion);
         Serial.print("FIRMWARE: found new firmware version "); Serial.println(stMSG);
         delay(1000);
         Serial.println( "FIRMWARE: starting installation..." );
         String fwImageURL = String(OTA_UpdateURL);
         fwImageURL.concat( OTA_SystemName );
         fwImageURL.concat( "_" );
         fwImageURL.concat( stMSG );
         fwImageURL.concat( ".bin" );
         // Add optional callback notifiers
         httpUpdate.onEnd(SCREEN_UpdatePopupFinished);
         httpUpdate.onProgress(SCREEN_UpdatePopupProgress);
         // Start firmware update
         t_httpUpdate_return ret = httpUpdate.update(client, fwImageURL);
         switch (ret) {
            case HTTP_UPDATE_FAILED:
               Serial.printf("FIRMWARE: %s\n", httpUpdate.getLastErrorString().c_str());
               SCREEN_UpdatePopupError(httpUpdate.getLastError());
               break;
            case HTTP_UPDATE_NO_UPDATES:
               Serial.println("FIRMWARE: no updates available");
               SCREEN_UpdatePopupUnavailable();
               break;
            case HTTP_UPDATE_OK:
               Serial.println("FIRMWARE: update success");
               break;
         }
      } else {
         // Firmware already the latest version
         Serial.println( "FIRMWARE: already on latest version, no update needed." );
      }
   } else {
      // Error downloading firmware version file
      Serial.print("FIRMWARE: version check failed, got HTTP response code: ");
      Serial.println(httpCode);
   }
   // Close the HTTP client
   httpClient.end();
   delay(500);
   yield();
   delay(500);
   yield();
   // Deactivate UPDATE icon on display
   tft.setFreeFont(FSSB12);
   tft.fillRect(187,4, 40,17, tft.color565(96,96,96));
   tft.setTextColor(TFT_BLACK);
   tft.drawString("UPDT", 192,4, 2);
}
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
void NetMQTT_Management(void *parameter) {
   unsigned long currTIME = millis();
   unsigned long lastNETMQTTTIME = 0;
   unsigned long lastSENDSTATUS = millis();
   for (;;) {
      // Store current time elapsed
      currTIME = millis();
      // Check every 1 SECOND for Network and MQTT status nad manage reconnections
      if ((unsigned long)(currTIME - lastNETMQTTTIME) >= 1000) { 
         // Store last time for next interval check
         lastNETMQTTTIME = currTIME;
         // ### Check the WIFI status ###
         if (WiFi.status() == WL_CONNECTED) {
            // Connection successful
            if (WIFI_Signal == 255) { MQTT_RetryAttempts = MQTT_RetryAttempts_TIMEOUT; }
            WIFI_SSID = WiFi.SSID();
            WIFI_Signal = 0;
            WIFI_RetryAttempts = 0;
            if (WiFi.RSSI() >= -105) { WIFI_Signal = 1; }
            if (WiFi.RSSI() >= -95) { WIFI_Signal = 2; }
            if (WiFi.RSSI() >= -85) { WIFI_Signal = 3; }
            if (WiFi.RSSI() >= -75) { WIFI_Signal = 4; }
            if (WiFi.RSSI() >= -65) { WIFI_Signal = 5; }
            // Check MQTT connection status
            if (mqttClient.connected()) { 
               MQTT_Status = 1; 
               MQTT_RetryAttempts = 0;
            } else {
               MQTT_Status = 0;
               MQTT_RetryAttempts +=1;
            if (MQTT_RetryAttempts >= MQTT_RetryAttempts_TIMEOUT) {
                  MQTT_RetryAttempts = 0; 
                  mqttClient.setKeepAlive(MQTT_KeepAlive_TIMEOUT);
                  mqttClient.setServer(MQTT_Server, 1883);
                  MQTT_Reconnect();
               }
            }
         } else {   
            // WIFI not connected
            MQTT_RetryAttempts = 0;
            MQTT_Status = 0;
            WIFI_SSID = "n/a";
            WIFI_Signal = 255;
            // Increment timeout counter after that try to reconnect
            WIFI_RetryAttempts += 1;
            if (WIFI_RetryAttempts >= WIFI_RetryAttempts_TIMEOUT) {
               WIFI_RetryAttempts = 0;
               WiFi.reconnect();
            }
         }
      }
      // Check if we have to start WPS pairing procedure
      if ((forceWPSpairing==1)&&(MENU_Section==0)) { WPS_Start(); }
      // Send thermostat status updates every 1 minute
      if (((unsigned long)(currTIME - lastSENDSTATUS) >= 60000)&&(MQTT_Status==1)) {
         // Send MQTT status update for all parameters
         MQTT_SendStatus();
         // Store current time for next check
         lastSENDSTATUS = currTIME;
      }
      // Pause the loop for half second
      yield();
      delay(500);
   }   
   // Delete parallel thread once exiting
   vTaskDelete(NULL);
}
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
void SCREEN_Layout() {
   char stDATA[40];
   // Check if menu selection has changed
   if (MENU_Section == MENU_Section_PREV) { return; }
   // Empty the display
   tft.fillScreen(ILI9341_BLACK);
   // Bordo schermo
   tft.drawLine( 44,  0, 319,  0, TFT_WHITE);
   tft.drawLine( 44,239, 319,239, TFT_WHITE);
   tft.drawLine( 44,  0,  44,239, TFT_WHITE);
   tft.drawLine(319,  0, 319,239, TFT_WHITE);
   // Area TABS selezione schermate
   if (MENU_Section != MENU_Section_PREV) {
      if (MENU_Section == 0) {
         tft.fillRect(0,0, 44,48, tft.color565(255,255,255));
         tft.fillRect(1,1, 42,46, tft.color565(0,230,23));
         tft.drawBitmap(7,9, ICON_Tab_Home, ICONS_Size,ICONS_Size, tft.color565(0,0,0), tft.color565(0,230,23));
      } else {
         tft.fillRect(0,0, 44,48, tft.color565(96,96,96));
         tft.fillRect(1,1, 42,46, tft.color565(0,0,0));
         tft.drawBitmap(7,9, ICON_Tab_Home, ICONS_Size,ICONS_Size, tft.color565(96,96,96), tft.color565(0,0,0));
      }
      if (MENU_Section == 1) {
         tft.fillRect(0,48, 44,48, tft.color565(255,255,255));
         tft.fillRect(1,49, 42,46, tft.color565(255,204,0));
         tft.drawBitmap(7,57, ICON_Tab_Mode, ICONS_Size,ICONS_Size, tft.color565(0,0,0), tft.color565(255,204,0));
      } else {
         tft.fillRect(0,48, 44,48, tft.color565(96,96,96));
         tft.fillRect(1,49, 42,46, tft.color565(0,0,0));
         tft.drawBitmap(7,57, ICON_Tab_Mode, ICONS_Size,ICONS_Size, tft.color565(96,96,96), tft.color565(0,0,0));
      }
      if (MENU_Section == 2) {
         tft.fillRect(0,96, 44,48, tft.color565(255,255,255));
         tft.fillRect(1,97, 42,46, tft.color565(0,132,255));
         tft.drawBitmap(7,105, ICON_Tab_Temperature, ICONS_Size,ICONS_Size, tft.color565(0,0,0), tft.color565(0,132,255));
      } else {
         tft.fillRect(0,96, 44,48, tft.color565(96,96,96));
         tft.fillRect(1,97, 42,46, tft.color565(0,0,0));
         tft.drawBitmap(7,105, ICON_Tab_Temperature, ICONS_Size,ICONS_Size, tft.color565(96,96,96), tft.color565(0,0,0));
      }
      if (MENU_Section == 3) {
         tft.fillRect(0,144, 44,48, tft.color565(255,255,255));
         tft.fillRect(1,145, 42,46, tft.color565(236,45,211));
         tft.drawBitmap(7,153, ICON_Tab_Schedule, ICONS_Size,ICONS_Size, tft.color565(0,0,0), tft.color565(236,45,211));
      } else {
         tft.fillRect(0,144, 44,48, tft.color565(96,96,96));
         tft.fillRect(1,145, 42,46, tft.color565(0,0,0));
         tft.drawBitmap(7,153, ICON_Tab_Schedule, ICONS_Size,ICONS_Size, tft.color565(96,96,96), tft.color565(0,0,0));
      }
      if (MENU_Section == 4) {
         tft.fillRect(0,192, 44,48, tft.color565(255,255,255));
         tft.fillRect(1,193, 42,46, tft.color565(255,0,0));
         tft.drawBitmap(7,201, ICON_Tab_Settings, ICONS_Size,ICONS_Size, tft.color565(0,0,0), tft.color565(255,0,0));
      } else {
         tft.fillRect(0,192, 44,48, tft.color565(96,96,96));
         tft.fillRect(1,193, 42,46, tft.color565(0,0,0));
         tft.drawBitmap(7,201, ICON_Tab_Settings, ICONS_Size,ICONS_Size, tft.color565(96,96,96), tft.color565(0,0,0));
      }
   }
   // Check which menu structure display
   switch (MENU_Section) {
      case 0:
         // #################
         // ### HOME MENU ###
         // #################
         // Display the line separator WIFI-MQTT/TIME-TEMP
         tft.drawLine(44,24, 319,24, TFT_WHITE);
         // Draw the SSID/STATUSES vertical separator
         tft.drawLine(182,1, 182,23, TFT_WHITE);
         // Display the CURRENT TIME icon
         tft.drawBitmap(49,33, ICON_Time, ICONS_Size,ICONS_Size, tft.color565(41,152,255), tft.color565(0,0,0));
         // Display the TEMPERATURE SET-POINT/HUMIDITY icon and symbols
         tft.drawBitmap(225,31, ICON16_SetPoint, ICONS16_Size,ICONS16_Size, tft.color565(41,255,121), tft.color565(0,0,0));
         tft.fillEllipse(310, 33, 3,3, tft.color565(41,255,121));
         tft.drawBitmap(225,51, ICON16_Humidity, ICONS16_Size,ICONS16_Size, tft.color565(41,255,121), tft.color565(0,0,0));
         tft.drawLine(309,32, 311,32, tft.color565(0,0,0));
         tft.drawLine(309,33, 311,33, tft.color565(0,0,0));
         tft.drawLine(309,34, 311,34, tft.color565(0,0,0));
         // Draw the TIME/TEMP-HUM vertical separator
         tft.drawLine(217,25, 217,71, TFT_WHITE);
         // Display the line separator TIME-TEMP / SET-POINT
         tft.drawLine(44,72, 319,72, TFT_WHITE);
         // Display TEMPERATURE degree symbol
         tft.setFreeFont(FSSB18);
         tft.setTextColor(TFT_WHITE);
         tft.drawString("o", 275,73, GFXFF);
         // Display the line separator SET-POINT / SCHEDULE
         tft.drawLine(44,191, 319,191, TFT_WHITE);
         // Display schedule legend placeholders
         tft.setFreeFont(TT1);
         tft.setTextColor(tft.color565(255,255,0));
         tft.drawString("T2",  52,199, 1);
         tft.drawString("T1",  52,210, 1);
         tft.drawString("T0",  52,221, 1);
         tft.drawString("T2", 305,199, 1);
         tft.drawString("T1", 305,210, 1);
         tft.drawString("T0", 305,221, 1);
         for (byte iii=0; iii<24; iii++) {
            tft.drawRect(63+(iii*10),197, 8,9, tft.color565(96,96,96));
            tft.drawRect(63+(iii*10),208, 8,9, tft.color565(96,96,96));
            tft.drawRect(63+(iii*10),219, 8,9, tft.color565(96,96,96));
            sprintf(stDATA, "%d", iii);
            tft.drawString(stDATA, (iii<10 ? 66 : 64)+(iii*10),231, 1);
         }
         break;
      case 1:
         // ###########################
         // ### WORK MODE SELECTION ###
         // ###########################
         tft.setFreeFont(FSSB12);
         tft.setTextColor(tft.color565(255,255,255));
         tft.drawString("Modalita'", 48,5, GFXFF);
         // Draw 4 button rectangles
         tft.drawRect(90, 40, 185,40, TFT_WHITE);   // Off
         tft.drawRect(90, 90, 185,40, TFT_WHITE);   // Manual
         tft.drawRect(90,140, 185,40, TFT_WHITE);   // Automatic
         tft.drawRect(90,190, 185,40, TFT_WHITE);   // Holiday
         break;
      case 2:
         // ############################
         // ### TEMPERATURE SETTINGS ###
         // ############################
         tft.setFreeFont(FSSB12);
         tft.setTextColor(tft.color565(255,255,255));
         tft.drawString("Temperature", 48,5, GFXFF);
         // Draw the TM/T2/T1/T0 labels
         tft.setFreeFont(FSSB12);
         tft.setTextColor(tft.color565(255,0,255));
         tft.drawString("TM =", 52,42, 1);
         tft.setTextColor(tft.color565(255,0,0));
         tft.drawString("T2 =", 52,97, 1);
         tft.setTextColor(tft.color565(255,255,0));
         tft.drawString("T1 =", 52,152, 1);
         tft.setTextColor(tft.color565(41,152,255));
         tft.drawString("T0 =", 52,207, 1);
         // Draw the +/- buttons near TM temperature setting
         tft.setTextColor(TFT_WHITE);
         tft.drawRect(214,34, 36,36, TFT_WHITE);  tft.fillRect(215,35, 34,34, tft.color565(255,0,0)); tft.drawString("+", 224,40, 1);
         tft.drawRect(274,34, 36,36, TFT_WHITE);  tft.fillRect(275,35, 34,34, tft.color565(0,128,0)); tft.drawString("-", 288,41, 1);
         // Draw the +/- buttons near T2 temperature setting
         tft.drawRect(214,89, 36,36, TFT_WHITE);  tft.fillRect(215,90, 34,34, tft.color565(255,0,0)); tft.drawString("+", 224,95, 1);
         tft.drawRect(274,89, 36,36, TFT_WHITE);  tft.fillRect(275,90, 34,34, tft.color565(0,128,0)); tft.drawString("-", 288,96, 1);
         // Draw the +/- buttons near T1 temperature setting
         tft.drawRect(214,144, 36,36, TFT_WHITE);  tft.fillRect(215,145, 34,34, tft.color565(255,0,0)); tft.drawString("+", 224,150, 1);
         tft.drawRect(274,144, 36,36, TFT_WHITE);  tft.fillRect(275,145, 34,34, tft.color565(0,128,0)); tft.drawString("-", 288,151, 1);
         // Draw the +/- buttons near T0 temperature setting
         tft.drawRect(214,199, 36,36, TFT_WHITE);  tft.fillRect(215,200, 34,34, tft.color565(255,0,0)); tft.drawString("+", 224,205, 1);
         tft.drawRect(274,199, 36,36, TFT_WHITE);  tft.fillRect(275,200, 34,34, tft.color565(0,128,0)); tft.drawString("-", 288,206, 1);
         break;
      case 3:
         // #########################
         // ### PROGRAMS SETTINGS ###
         // #########################
         tft.setFreeFont(FSSB12);
         tft.setTextColor(TFT_WHITE);
         tft.drawString("Programma", 48,5, GFXFF);
         // Draw the DEFAULT button
         tft.drawRect(229,36, 71,36, TFT_WHITE);  tft.fillRect(230,37, 69,34, tft.color565(160,0,160)); tft.drawString("Reset", 231,45, 1);
         // Draw the D+/D- buttons
         tft.drawRect( 74,141, 36,36, TFT_WHITE);  tft.fillRect( 75,142, 34,34, tft.color565(41,152,255)); tft.drawString("D+",  76,150, 1);
         tft.drawRect( 74,191, 36,36, TFT_WHITE);  tft.fillRect( 75,192, 34,34, tft.color565(41,152,255)); tft.drawString("D-",  79,200, 1);
         // Draw the H+/H- buttons
         tft.drawRect(139,166, 36,36, TFT_WHITE);  tft.fillRect(140,167, 34,34, tft.color565(0,128,0)); tft.drawString("H-", 143,175, 1);
         tft.drawRect(199,166, 36,36, TFT_WHITE);  tft.fillRect(200,167, 34,34, tft.color565(0,128,0)); tft.drawString("H+", 201,175, 1);
         // Draw the T1/T2 buttons
         tft.drawRect(264,141, 36,36, TFT_WHITE);  tft.fillRect(265,142, 34,34, tft.color565(255,0,0)); tft.drawString("T2", 267,150, 1);
         tft.drawRect(264,191, 36,36, TFT_WHITE);  tft.fillRect(265,192, 34,34, tft.color565(255,128,0)); tft.drawString("T1", 268,200, 1);
         // Display schedule legend placeholders
         tft.setFreeFont(TT1);
         tft.setTextColor(tft.color565(255,255,0));
         tft.drawString("T2",  52, 89, 1);
         tft.drawString("T1",  52,100, 1);
         tft.drawString("T0",  52,111, 1);
         tft.drawString("T2", 305, 89, 1);
         tft.drawString("T1", 305,100, 1);
         tft.drawString("T0", 305,111, 1);
         for (byte iii=0; iii<24; iii++) {
            tft.drawRect(63+(iii*10), 87, 8,9, tft.color565(96,96,96));
            tft.drawRect(63+(iii*10), 98, 8,9, tft.color565(96,96,96));
            tft.drawRect(63+(iii*10),109, 8,9, tft.color565(96,96,96));
            sprintf(stDATA, "%d", iii);
            tft.drawString(stDATA, (iii<10 ? 66 : 64)+(iii*10),121, 1);
         }
         break;
      case 4:
         // ########################
         // ### GENERAL SETTINGS ###
         // ########################
         // Display the current firmware version
         tft.setFreeFont(FSSB12);
         tft.setTextColor(tft.color565(255,255,255));
         tft.drawString("Impostazioni", 48,5, GFXFF);
         sprintf(stDATA, "(FW: %.02f)", OTA_CurrentVersion);
         tft.drawRightString(stDATA, 315, 6, 2);
         // Draw the TIME/DATE icon
         tft.drawBitmap(49,41, ICON_Time, ICONS_Size,ICONS_Size, tft.color565(41,152,255), tft.color565(0,0,0));
         // Draw the WIFI WPS SETUP button
         tft.drawRect(224,36, 80,32, TFT_WHITE);
         tft.fillRect(225,37, 78,30, tft.color565(255,0,255));
         tft.setFreeFont(FSSB12);
         tft.setTextColor(TFT_WHITE);
         tft.drawString("WPS Setup",232,45, 2);
         // Draw the WIFI WPS RESET button
         tft.drawRect(224,82, 80,32, TFT_WHITE);
         tft.fillRect(225,83, 78,30, tft.color565(255,0,0));
         tft.setFreeFont(FSSB12);
         tft.setTextColor(TFT_WHITE);
         tft.drawString("WPS Reset",232,91, 2);
         // Draw the FIRMWARE UPDATE setup button
         tft.drawRect(224,128, 80,32, TFT_WHITE);
         tft.fillRect(225,129, 78,30, tft.color565(255,128,0));
         tft.setFreeFont(FSSB12);
         tft.setTextColor(TFT_WHITE);
         tft.drawString("FW Update",232,137, 2);
         // Draw the MQTT icon
         tft.drawBitmap(49,88, ICON_MQTT, 37,16, tft.color565(0,128,0), tft.color565(0,0,0));
         // Draw the TEMPERATURE GAIN icon
         tft.drawBitmap(49,110, ICON_TempGain, 37,16, tft.color565(160,0,160), tft.color565(0,0,0));
         // Draw the HUMIDITY GAIN icon
         tft.drawBitmap(49,132, ICON_HumiGain, 37,16, tft.color565(156,98,0), tft.color565(0,0,0));
         // Draw the current MAC Address
         tft.setFreeFont(FSSB12);
         tft.setTextColor(tft.color565(160,160,160));
         sprintf(stDATA, "Mac: %02X:%02X:%02X:%02X:%02X:%02X", MacAddress[0],MacAddress[1],MacAddress[2],MacAddress[3],MacAddress[4],MacAddress[5]);
         tft.drawString(stDATA, 50,154, 2);
         // Draw the LEFT/RIGHT/+/- buttons
         tft.setTextColor(TFT_WHITE);
         tft.drawRect( 74,191, 36,36, TFT_WHITE);  tft.fillRect( 75,192, 34,34, tft.color565(41,152,255)); tft.drawString("<",  84,197, 1);
         tft.drawRect(134,191, 36,36, TFT_WHITE);  tft.fillRect(135,192, 34,34, tft.color565(41,152,255)); tft.drawString(">", 148,198, 1);
         tft.drawRect(204,191, 36,36, TFT_WHITE);  tft.fillRect(205,192, 34,34, tft.color565(255,0,0)); tft.drawString("+", 214,197, 1);
         tft.drawRect(264,191, 36,36, TFT_WHITE);  tft.fillRect(265,192, 34,34, tft.color565(0,128,0)); tft.drawString("-", 278,198, 1);
         break;
   }   
   // Store the new menu section as previous one to avoid further updates
   MENU_Section_PREV = MENU_Section;
}
//---------------------------------------------------------------------------------------------------------
void SCREEN_Data(bool forceRefresh = false) {
   char stDATA[20];
   char stUNIT[20];
   byte myCH = 0;
   int myX = 0;
   // Check which menu is selected
   switch (MENU_Section) {
      case 0:
         // #################
         // ### HOME MENU ###
         // #################
         // Display the WIFI SSID & Signal status
         if ((WIFI_Signal != WIFI_Signal_PREV)||(WIFI_SSID != WIFI_SSID_PREV)||(forceRefresh == true)||(forceWPSpairing!=0)) {
            // Display the WIFI signal indicator
            tft.setFreeFont(FSSB12);
            tft.setTextColor(WIFI_Signal==255 ? tft.color565(255,64,64) : TFT_BLACK);
            tft.drawString("x",50,-1, 2); tft.drawString("x",51,0, 2);
            tft.fillRect(51,16, 4,5, ((WIFI_Signal>=1 && WIFI_Signal!=255) ? tft.color565(0,255,0) : tft.color565(96,96,96)));
            tft.fillRect(56,13, 4,8, ((WIFI_Signal>=2 && WIFI_Signal!=255) ? tft.color565(0,255,0) : tft.color565(96,96,96)));
            tft.fillRect(61,10, 4,11, ((WIFI_Signal>=3 && WIFI_Signal!=255) ? tft.color565(0,255,0) : tft.color565(96,96,96)));
            tft.fillRect(66,7, 4,14, ((WIFI_Signal>=4 && WIFI_Signal!=255) ? tft.color565(0,255,0) : tft.color565(96,96,96)));
            tft.fillRect(71,4, 4,17, ((WIFI_Signal>=5 && WIFI_Signal!=255) ? tft.color565(0,255,0) : tft.color565(96,96,96)));
            tft.fillRect(79,6, 103,17, TFT_BLACK);
            tft.setTextColor(TFT_WHITE);
            switch (forceWPSpairing) {
               case 1:
                  // WPS Start
                  tft.drawString("WPS Pairing", 79,6, 2);
                  break;
               case 2:
                  // WPS Success
                  tft.drawString("WPS OK", 79,6, 2);
                  break;
               case 3:
                  // WPS Failed
                  tft.drawString("WPS Error", 79,6, 2);
                  break;
               case 4:
                  // WPS Timeout
                  tft.drawString("WPS Timeout", 79,6, 2);
                  break;
               default:
                  // WIFI active
                  tft.drawString(WIFI_SSID, 79,6, 2);
                  break;
            }
         }   
         // Display the UPDATE status indicator
         if ((UPDT_Status != UPDT_Status_PREV)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB12);
            tft.fillRect(187,4, 40,17, (UPDT_Status==1 ? tft.color565(255,128,0) : tft.color565(96,96,96)));
            tft.setTextColor((UPDT_Status==1 ? TFT_WHITE : TFT_BLACK));
            tft.drawString("UPDT", 192,4, 2);
         }
         // Display the WIFI status indicator
         if ((WIFI_Signal != WIFI_Signal_PREV)||(WIFI_SSID != WIFI_SSID_PREV)||(forceRefresh == true)||(forceWPSpairing!=0)) {   
            tft.setFreeFont(FSSB12);
            tft.fillRect(231,4, 40,17, (WIFI_Signal!=255 ? tft.color565(0,128,0) : tft.color565(96,96,96)));
            tft.setTextColor((WIFI_Signal!=255 ? TFT_WHITE : TFT_BLACK));
            tft.drawString("WiFi", 239,4, 2);
         }
         // Display the MQTT status indicator
         if ((MQTT_Status != MQTT_Status_PREV)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB12);
            tft.fillRect(275,4, 40,17, (MQTT_Status==1 ? tft.color565(41,152,255) : tft.color565(96,96,96)));
            tft.setTextColor((MQTT_Status==1 ? TFT_WHITE : TFT_BLACK));
            tft.drawString("MQTT", 279,4, 2);
         }
         // Display the current TIME
         if ((strcmp(TIME, TIME_PREV) != 0)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB12);
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawString(TIME_PREV, 84,29, GFXFF);
            tft.setTextColor(tft.color565(41,152,255));
            tft.drawString(TIME, 84,29, GFXFF);
         }   
         // Display the current DAY-OF-WEEK
         if ((strcmp(DAY, DAY_PREV) != 0)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB12);
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawString(DAY_PREV, 147,33, 2);
            tft.setTextColor(tft.color565(41,152,255));
            tft.drawString(DAY, 147,33, 2);
         }
         // Display the current DATE   
         if ((strcmp(DATE, DATE_PREV) != 0)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB12);
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawString(DATE_PREV, 84,50, GFXFF);
            tft.setTextColor(tft.color565(41,152,255));
            tft.drawString(DATE, 84,50, GFXFF);
         }
         // Display the TEMPERATURE SETPOINT (old CURRENT TEMPERATURE)
         if ((SETPOINT != SETPOINT_PREV)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB12);
            sprintf(stDATA, "%0.1f", SETPOINT_PREV);
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawRightString(stDATA, 305,29, GFXFF);
            sprintf(stDATA, "%0.1f", SETPOINT);
            tft.setTextColor(tft.color565(41,255,121));
            tft.drawRightString(stDATA, 305,29, GFXFF);
         }
         // Display the CURRENT HUMIDITY
         if ((HUMIDITY != HUMIDITY_PREV)||(SI7021_DATAVALID_PREV != SI7021_DATAVALID)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB12);
            if (SI7021_DATAVALID_PREV==1) { sprintf(stDATA, "%d%%", HUMIDITY_PREV); } else { sprintf(stDATA, "---%%"); }
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawRightString(stDATA, 315,50, GFXFF);
            if (SI7021_DATAVALID==1) { sprintf(stDATA, "%d%%", HUMIDITY); } else { sprintf(stDATA, "---%%"); }
            tft.setTextColor(tft.color565(41,255,121));
            tft.drawRightString(stDATA, 315,50, GFXFF);
         }
         // Display the CURRENT TEMPERATURE (old TEMPERATURE SETPOINT)
         if ((TEMPERATURE != TEMPERATURE_PREV)||(SI7021_DATAVALID_PREV != SI7021_DATAVALID)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB24);
            if (SI7021_DATAVALID_PREV==1) { sprintf(stDATA, "%0.1f", TEMPERATURE_PREV); } else { sprintf(stDATA, "--.-"); }
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawRightString(stDATA, 270,76, 8);
            if (SI7021_DATAVALID==1) { sprintf(stDATA, "%0.1f", TEMPERATURE); } else { sprintf(stDATA, "--.-"); }
            tft.setTextColor(TFT_WHITE);
            tft.drawRightString(stDATA, 270,76, 8);
         }
         // Display HEATER-ON icon status
         if ((RELE_Status != RELE_Status_PREV)||(forceRefresh == true)) {
            if (RELE_Status==1) {
               tft.drawBitmap(275,115, ICON_HeaterOn, ICONS_Size,ICONS_Size, tft.color565(255,0,0), tft.color565(0,0,0));
            } else {
               tft.drawBitmap(275,115, ICON_HeaterOff, ICONS_Size,ICONS_Size, tft.color565(96,96,96), tft.color565(0,0,0));
            }
         }
         // Display WORKING-MODE icon and status
         if ((THERMO_Mode != THERMO_Mode_PREV)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB12);
            tft.setTextColor(tft.color565(0,0,0));
            switch (THERMO_Mode_PREV) {
               case 1: tft.fillRect(110,155, 32,32, tft.color565(0,0,0)); tft.drawString("Manuale", 150,162, GFXFF); break;
               case 2: tft.fillRect(95,155, 32,32, tft.color565(0,0,0)); tft.drawString("Automatico", 135,162, GFXFF); break;
               case 3: tft.fillRect(120,155, 32,32, tft.color565(0,0,0)); tft.drawString("Holiday", 160,162, GFXFF); break;
               default: tft.fillRect(120,155, 32,32, tft.color565(0,0,0)); tft.drawString("Spento", 160,162, GFXFF); break;
            }
            switch (THERMO_Mode) {
               case 1: 
                  tft.drawBitmap(110,155, ICON_ModeManual, ICONS_Size,ICONS_Size, tft.color565(247,124,15), tft.color565(0,0,0));
                  tft.setTextColor(tft.color565(247,124,15)); tft.drawString("Manuale", 150,162, GFXFF);
                  break;
               case 2: 
                  tft.drawBitmap(95,155, ICON_ModeAuto, ICONS_Size,ICONS_Size, tft.color565(0,191,243), tft.color565(0,0,0));
                  tft.setTextColor(tft.color565(0,191,243)); tft.drawString("Automatico", 135,162, GFXFF);
                  break;
               case 3: 
                  tft.drawBitmap(120,155, ICON_ModeHoliday, ICONS_Size,ICONS_Size, tft.color565(236,45,211), tft.color565(0,0,0));
                  tft.setTextColor(tft.color565(236,45,211)); tft.drawString("Holiday", 160,162, GFXFF);
                  break;
               default: 
                  tft.drawBitmap(120,155, ICON_ModeOff, ICONS_Size,ICONS_Size, tft.color565(128,128,128), tft.color565(0,0,0));
                  tft.setTextColor(tft.color565(128,128,128)); tft.drawString("Spento", 160,162, GFXFF);
                  break;
            }
         }
         // Display the WORKING-SCHEDULE active
         for (byte iii=0; iii<24; iii++) {
            switch (DAY_NUM) {
               case 2:  myCH = THERMO_Schedule_1[iii]; break;
               case 3:  myCH = THERMO_Schedule_2[iii]; break;
               case 4:  myCH = THERMO_Schedule_3[iii]; break;
               case 5:  myCH = THERMO_Schedule_4[iii]; break;
               case 6:  myCH = THERMO_Schedule_5[iii]; break;
               case 7:  myCH = THERMO_Schedule_6[iii]; break;
               default: myCH = THERMO_Schedule_7[iii]; break;
            }
            if (THERMO_Mode==3) { myCH = THERMO_Schedule_H[iii]; } // Holiday schedule
            if (THERMO_Mode==1) { myCH = 2; } // Always T2 temp for MANUAL mode
            if (THERMO_Mode==0) { myCH = 0; } // Always OFF
            // Draw each schedule square placeholder
            if (myCH==0) { 
               if (TIME_HOUR==iii) {
                  // Filled T0 rectangle - CURRENT HOUR
                  tft.fillRect(64+(iii*10),220, 6,7, tft.color565(255,0,0));
               } else {
                  // Filled T0 rectangle
                  tft.fillRect(64+(iii*10),220, 6,7, tft.color565(0,255,0));
               }
            } else { 
               // Empty T0 rectangle
               tft.fillRect(64+(iii*10),220, 6,7, tft.color565(0,0,0));
            }
            if (myCH==1) { 
               if (TIME_HOUR==iii) {
                  // Filled T1 rectangle - CURRENT HOUR
                  tft.fillRect(64+(iii*10),209, 6,7, tft.color565(255,0,0));
               } else {
                  // Filled T1 rectangle
                  tft.fillRect(64+(iii*10),209, 6,7, tft.color565(0,255,0));
               }
            } else { 
               // Empty T1 rectangle
               tft.fillRect(64+(iii*10),209, 6,7, tft.color565(0,0,0));
            }
            if (myCH==2) { 
               if (TIME_HOUR==iii) { 
                  // Filled T2 rectangle - CURRENT HOUR
                  tft.fillRect(64+(iii*10),198, 6,7, tft.color565(255,0,0)); 
               } else { 
                  // Filled T2 rectangle
                  tft.fillRect(64+(iii*10),198, 6,7, tft.color565(0,255,0)); 
               }
            } else { 
               // Empty T2 rectangle
               tft.fillRect(64+(iii*10),198, 6,7, tft.color565(0,0,0));
            }
         }
         break;
      case 1:
         // ###########################
         // ### WORK MODE SELECTION ###
         // ###########################
         if ((THERMO_Mode != THERMO_Mode_PREV)||(forceRefresh == true)) {
            // Fill active mode rectangle and empty others
            tft.fillRect(91, 41, 183,38, ((THERMO_Mode==0) ? tft.color565(128,128,128) : TFT_BLACK ));   // Off
            tft.fillRect(91, 91, 183,38, ((THERMO_Mode==1) ? tft.color565(247,124,15) : TFT_BLACK ));    // Manual
            tft.fillRect(91,141, 183,38, ((THERMO_Mode==2) ? tft.color565(0,191,243) : TFT_BLACK ));   // Automatic
            tft.fillRect(91,191, 183,38, ((THERMO_Mode==3) ? tft.color565(236,45,211) : TFT_BLACK ));   // Holiday
            // Place ICONS of each mode in color based on selection
            tft.drawBitmap(121,44, ICON_ModeOff, ICONS_Size,ICONS_Size, (THERMO_Mode==0 ? TFT_BLACK : tft.color565(128,128,128)), (THERMO_Mode==0 ? tft.color565(128,128,128) : TFT_BLACK));
            tft.drawBitmap(111,94, ICON_ModeManual, ICONS_Size,ICONS_Size, (THERMO_Mode==1 ? TFT_BLACK : tft.color565(247,124,15)), (THERMO_Mode==1 ? tft.color565(247,124,15) : TFT_BLACK));
            tft.drawBitmap(96,144, ICON_ModeAuto, ICONS_Size,ICONS_Size, (THERMO_Mode==2 ? TFT_BLACK : tft.color565(0,191,243)), (THERMO_Mode==2 ? tft.color565(0,191,243) : TFT_BLACK));
            tft.drawBitmap(116,194, ICON_ModeHoliday, ICONS_Size,ICONS_Size, (THERMO_Mode==3 ? TFT_BLACK : tft.color565(236,45,211)), (THERMO_Mode==3 ? tft.color565(236,45,211) : TFT_BLACK));
            // Place TEXT of each mode in color based on selection
            tft.setTextColor((THERMO_Mode==0) ? TFT_BLACK : tft.color565(128,128,128)); tft.drawString("Spento", 159,51, GFXFF);
            tft.setTextColor((THERMO_Mode==1) ? TFT_BLACK : tft.color565(247,124,15)); tft.drawString("Manuale", 148,101, GFXFF);
            tft.setTextColor((THERMO_Mode==2) ? TFT_BLACK : tft.color565(0,191,243)); tft.drawString("Automatico", 133,151, GFXFF);
            tft.setTextColor((THERMO_Mode==3) ? TFT_BLACK : tft.color565(236,45,211)); tft.drawString("Holiday", 154,201, GFXFF);
         }
         break;
      case 2:
         // ############################
         // ### TEMPERATURE SETTINGS ###
         // ############################
         // Display the TEMPERATURE SETPOINT TM
         if ((THERMO_TM != THERMO_TM_PREV)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB24);
            sprintf(stDATA, "%0.1f", THERMO_TM_PREV);
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawRightString(stDATA, 200,33, 1);
            sprintf(stDATA, "%0.1f", THERMO_TM);
            tft.setTextColor(tft.color565(255,0,255));
            tft.drawRightString(stDATA, 200,33, 1);
         }
         // Display the TEMPERATURE SETPOINT T2
         if ((THERMO_T2 != THERMO_T2_PREV)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB24);
            sprintf(stDATA, "%0.1f", THERMO_T2_PREV);
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawRightString(stDATA, 200,88, 1);
            sprintf(stDATA, "%0.1f", THERMO_T2);
            tft.setTextColor(tft.color565(255,0,0));
            tft.drawRightString(stDATA, 200,88, 1);
         }
         // Display the TEMPERATURE SETPOINT T1
         if ((THERMO_T1 != THERMO_T1_PREV)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB24);
            sprintf(stDATA, "%0.1f", THERMO_T1_PREV);
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawRightString(stDATA, 200,143, 1);
            sprintf(stDATA, "%0.1f", THERMO_T1);
            tft.setTextColor(tft.color565(255,255,0));
            tft.drawRightString(stDATA, 200,143, 1);
         }
         // Display the TEMPERATURE SETPOINT T0
         if ((THERMO_T0 != THERMO_T0_PREV)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB24);
            sprintf(stDATA, "%0.1f", THERMO_T0_PREV);
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawRightString(stDATA, 200,198, 1);
            sprintf(stDATA, "%0.1f", THERMO_T0);
            tft.setTextColor(tft.color565(41,152,255));
            tft.drawRightString(stDATA, 200,198, 1);
         }

         break;
      case 3:
         // #########################
         // ### PROGRAMS SETTINGS ###
         // #########################
         // Display the current selected day if changed
         if ((SETTINGS_PositionY!=SETTINGS_PositionY_PREV)||(forceRefresh == true)) {
            tft.setFreeFont(FSSB12);
            switch (SETTINGS_PositionY_PREV) {
               case 0: sprintf(stDATA, "Lunedi'"); break;
               case 1: sprintf(stDATA, "Martedi'"); break;
               case 2: sprintf(stDATA, "Mercoledi'"); break;
               case 3: sprintf(stDATA, "Giovedi'"); break;
               case 4: sprintf(stDATA, "Venerdi'"); break;
               case 5: sprintf(stDATA, "Sabato"); break;
               case 6: sprintf(stDATA, "Domenica"); break;
               case 7: sprintf(stDATA, "Holiday"); break;
            }
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawString(stDATA, 55,45, GFXFF);
            switch (SETTINGS_PositionY) {
               case 0: sprintf(stDATA, "Lunedi'"); break;
               case 1: sprintf(stDATA, "Martedi'"); break;
               case 2: sprintf(stDATA, "Mercoledi'"); break;
               case 3: sprintf(stDATA, "Giovedi'"); break;
               case 4: sprintf(stDATA, "Venerdi'"); break;
               case 5: sprintf(stDATA, "Sabato"); break;
               case 6: sprintf(stDATA, "Domenica"); break;
               case 7: sprintf(stDATA, "Holiday"); break;
            }
            tft.setTextColor(tft.color565(255,255,0));
            tft.drawString(stDATA, 55,45, GFXFF);
         }
         // Display the WORKING-SCHEDULE active
         if ((SETTINGS_PositionX!=SETTINGS_PositionX_PREV)||(forceRefresh == true)) {
            for (byte iii=0; iii<24; iii++) {
               switch (SETTINGS_PositionY) {
                  case 0: myCH = THERMO_Schedule_1[iii]; break;
                  case 1: myCH = THERMO_Schedule_2[iii]; break;
                  case 2: myCH = THERMO_Schedule_3[iii]; break;
                  case 3: myCH = THERMO_Schedule_4[iii]; break;
                  case 4: myCH = THERMO_Schedule_5[iii]; break;
                  case 5: myCH = THERMO_Schedule_6[iii]; break;
                  case 6: myCH = THERMO_Schedule_7[iii]; break;
                  case 7: myCH = THERMO_Schedule_H[iii]; break;
               }
               // Draw each schedule square placeholder
               if (myCH==0) { 
                  if (SETTINGS_PositionX==iii) {
                     // Filled T0 rectangle - CURRENT HOUR
                     tft.fillRect(64+(iii*10),110, 6,7, tft.color565(255,0,0));
                  } else {
                     // Filled T0 rectangle
                     tft.fillRect(64+(iii*10),110, 6,7, tft.color565(0,255,0));
                  }
               } else { 
                  // Empty T0 rectangle
                  tft.fillRect(64+(iii*10),110, 6,7, tft.color565(0,0,0));
               }
               if (myCH==1) { 
                  if (SETTINGS_PositionX==iii) {
                     // Filled T1 rectangle - CURRENT HOUR
                     tft.fillRect(64+(iii*10),99, 6,7, tft.color565(255,0,0));
                  } else {
                     // Filled T1 rectangle
                     tft.fillRect(64+(iii*10),99, 6,7, tft.color565(0,255,0));
                  }
               } else { 
                  // Empty T1 rectangle
                  tft.fillRect(64+(iii*10),99, 6,7, tft.color565(0,0,0));
               }
               if (myCH==2) { 
                  if (SETTINGS_PositionX==iii) { 
                     // Filled T2 rectangle - CURRENT HOUR
                     tft.fillRect(64+(iii*10),88, 6,7, tft.color565(255,0,0)); 
                  } else { 
                     // Filled T2 rectangle
                     tft.fillRect(64+(iii*10),88, 6,7, tft.color565(0,255,0)); 
                  }
               } else { 
                  // Empty T2 rectangle
                  tft.fillRect(64+(iii*10),88, 6,7, tft.color565(0,0,0));
               }
            }
         }
         break;
      case 4:
         // ########################
         // ### GENERAL SETTINGS ###
         // ########################
         // Display the current TIME
         if ((strcmp(TIME, TIME_PREV) != 0)||(forceRefresh == true)||(SETTINGS_PositionX != SETTINGS_PositionX_PREV)) {
            tft.setFreeFont(FSSB12);
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawString(TIME_PREV, 86,36, GFXFF);
            tft.fillRect(86,36, 28,19, (SETTINGS_PositionX==0 ? tft.color565(255,255,255) : tft.color565(0,0,0)));
            tft.fillRect(119,36, 28,19, (SETTINGS_PositionX==1 ? tft.color565(255,255,255) : tft.color565(0,0,0)));
            tft.setTextColor(tft.color565(41,152,255));
            tft.drawString(TIME, 86,36, GFXFF);
         }   
         // Display the current DAY-OF-WEEK
         if ((strcmp(DAY, DAY_PREV) != 0)||(forceRefresh == true)||(SETTINGS_PositionX != SETTINGS_PositionX_PREV)) {
            tft.setFreeFont(FSSB12);
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawString(DAY_PREV, 151,39, 2);
            tft.fillRect(150,40, 27,13, (SETTINGS_PositionX==2 ? tft.color565(255,255,255) : tft.color565(0,0,0)));
            tft.setTextColor(tft.color565(41,152,255));
            tft.drawString(DAY, 151,39, 2);
         }
         // Display the current DATE   
         if ((strcmp(DATE, DATE_PREV) != 0)||(forceRefresh == true)||(SETTINGS_PositionX != SETTINGS_PositionX_PREV)) {
            tft.setFreeFont(FSSB12);
            tft.setTextColor(tft.color565(0,0,0));
            tft.drawString(DATE_PREV, 86,59, GFXFF);
            tft.fillRect(86,59, 27,19, (SETTINGS_PositionX==3 ? tft.color565(255,255,255) : tft.color565(0,0,0)));
            tft.fillRect(121,59, 27,19, (SETTINGS_PositionX==4 ? tft.color565(255,255,255) : tft.color565(0,0,0)));
            tft.fillRect(153,59, 55,19, (SETTINGS_PositionX==5 ? tft.color565(255,255,255) : tft.color565(0,0,0)));
            tft.setTextColor(tft.color565(41,152,255));
            tft.drawString(DATE, 86,59, GFXFF);
         }
         // Display the current MQTT Address
         if ((MQTT_Server[0] != MQTT_Server_PREV[0])||(MQTT_Server[1] != MQTT_Server_PREV[1])||(MQTT_Server[2] != MQTT_Server_PREV[2])||(MQTT_Server[3] != MQTT_Server_PREV[3])||(forceRefresh == true)||(SETTINGS_PositionX != SETTINGS_PositionX_PREV)) {
            tft.setFreeFont(FSSB12);
            tft.setTextColor(tft.color565(0,0,0));
            sprintf(stDATA, "%03d.%03d.%03d.%03d", MQTT_Server_PREV[0],MQTT_Server_PREV[1],MQTT_Server_PREV[2],MQTT_Server_PREV[3]);
            tft.drawString(stDATA, 91,88, 2);
            tft.fillRect( 90,88, 25,15, (SETTINGS_PositionX==6 ? TFT_WHITE : TFT_BLACK));
            tft.fillRect(118,88, 25,15, (SETTINGS_PositionX==7 ? TFT_WHITE : TFT_BLACK));
            tft.fillRect(147,88, 25,15, (SETTINGS_PositionX==8 ? TFT_WHITE : TFT_BLACK));
            tft.fillRect(176,88, 25,15, (SETTINGS_PositionX==9 ? TFT_WHITE : TFT_BLACK));
            tft.setTextColor(tft.color565(0,128,0));
            sprintf(stDATA, "%03d.%03d.%03d.%03d", MQTT_Server[0],MQTT_Server[1],MQTT_Server[2],MQTT_Server[3]);
            tft.drawString(stDATA, 91,88, 2);
         }
         // Display the TEMPERATURE GAIN value
         if ((TEMP_GAIN != TEMP_GAIN_PREV)||(forceRefresh == true)||(SETTINGS_PositionX != SETTINGS_PositionX_PREV)) {
            tft.setFreeFont(FSSB12);
            tft.setTextColor(TFT_BLACK);
            sprintf(stDATA, "%0.2f (T: %0.1f^)  ", (float(TEMP_GAIN_PREV)/100.0f),TEMPERATURE_PREV);
            tft.drawString(stDATA, 91,110, 2);
            tft.fillRect( 90,110, 30,15, (SETTINGS_PositionX==10 ? TFT_WHITE : TFT_BLACK));
            tft.setTextColor(tft.color565(160,0,160));
            sprintf(stDATA, "%0.2f (T: %0.1f^)  ",(float(TEMP_GAIN)/100.0f),TEMPERATURE);
            tft.drawString(stDATA, 91,110, 2);
         }
         // Display the HUMIDITY GAIN value
         if ((HUMI_GAIN != HUMI_GAIN_PREV)||(forceRefresh == true)||(SETTINGS_PositionX != SETTINGS_PositionX_PREV)) {
            tft.setFreeFont(FSSB12);
            tft.setTextColor(TFT_BLACK);
            sprintf(stDATA, "%0.2f (H: %d%%)  ", (float(HUMI_GAIN_PREV)/100.0f),HUMIDITY_PREV);
            tft.drawString(stDATA, 91,132, 2);
            tft.fillRect( 90,132, 30,15, (SETTINGS_PositionX==11 ? TFT_WHITE : TFT_BLACK));
            tft.setTextColor(tft.color565(156,98,0));
            sprintf(stDATA, "%0.2f (H: %d%%)  ",(float(HUMI_GAIN)/100.0f),HUMIDITY);
            tft.drawString(stDATA, 91,132, 2);
         }
         break;
   }
   // Store all new values to previous buffers
   UPDT_Status_PREV = UPDT_Status;
   WIFI_Signal_PREV = WIFI_Signal;
   WIFI_SSID_PREV = WIFI_SSID;
   MQTT_Status_PREV = MQTT_Status;
   strcpy(TIME_PREV, TIME);
   strcpy(DAY_PREV, DAY);
   strcpy(DATE_PREV, DATE);
   TEMPERATURE_PREV = TEMPERATURE;
   HUMIDITY_PREV = HUMIDITY;
   SI7021_DATAVALID_PREV = SI7021_DATAVALID;
   SETPOINT_PREV = SETPOINT;
   RELE_Status_PREV = RELE_Status;
   THERMO_Mode_PREV = THERMO_Mode;
   THERMO_TM_PREV = THERMO_TM;
   THERMO_T0_PREV = THERMO_T0;
   THERMO_T1_PREV = THERMO_T1;
   THERMO_T2_PREV = THERMO_T2;
   MQTT_Server_PREV[0] = MQTT_Server[0];
   MQTT_Server_PREV[1] = MQTT_Server[1];
   MQTT_Server_PREV[2] = MQTT_Server[2];
   MQTT_Server_PREV[3] = MQTT_Server[3];
   TEMP_GAIN_PREV = TEMP_GAIN;
   HUMI_GAIN_PREV = HUMI_GAIN;
   // Update saved previous setup menu entry
   SETTINGS_PositionX_PREV = SETTINGS_PositionX;
   SETTINGS_PositionY_PREV = SETTINGS_PositionY;
   // Reset the scheduler change flag if active
   THERMO_Schedule_CHANGE = 0;
}
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
void THERMOSTAT_Update() {
   byte myCH = 0;
   // Calculate the current SET-POINT based on program mode/time
   switch (THERMO_Mode) {
      case 0:
         // ### OFF ###
         // Store the T0 temperature as Set-Point
         SETPOINT = THERMO_T0;
         break;
      case 1:
         // ### MANUAL ###
         // Store the T2 temperature as Set-Point
         SETPOINT = THERMO_TM;
         break;
      case 2:
         // ### AUTO ###
         // Get schedule setting from weekly program for today at current hour
         switch (DAY_NUM) {
            case 2:  myCH = THERMO_Schedule_1[TIME_HOUR]; break;
            case 3:  myCH = THERMO_Schedule_2[TIME_HOUR]; break;
            case 4:  myCH = THERMO_Schedule_3[TIME_HOUR]; break;
            case 5:  myCH = THERMO_Schedule_4[TIME_HOUR]; break;
            case 6:  myCH = THERMO_Schedule_5[TIME_HOUR]; break;
            case 7:  myCH = THERMO_Schedule_6[TIME_HOUR]; break;
            default: myCH = THERMO_Schedule_7[TIME_HOUR]; break;
         }
         // Obtain the Set-Point value from schedule setting
         switch (myCH) {
            case 1: SETPOINT = THERMO_T1; break;
            case 2: SETPOINT = THERMO_T2; break;
            default: SETPOINT = THERMO_T0; break;
         }
         break;
      case 3:
         // ### HOLIDAY ###
         // Get schedule setting from holiday program at current hour
         myCH = THERMO_Schedule_H[TIME_HOUR];
         // Obtain the Set-Point value from schedule setting
         switch (myCH) {
            case 1:SETPOINT = THERMO_T1; break;
            case 2: SETPOINT = THERMO_T2; break;
            default: SETPOINT = THERMO_T0; break;
         }
         break;
   }
   // Process thermostat rele actuation only if temperature read is valid
   if (SI7021_DATAVALID == 1) {
      // Check if we have to turn ON or OFF the relay based of SET-POINT, CURRENT-TEMPERATURE and HYSTERESIS values
      if ((RELE_Status==1)&&(TEMPERATURE>=(SETPOINT+TEMP_HYSTERESIS_HI))) { RELE_Status = 0; Serial.println("RUNNING: thermostat rele OFF."); Serial.flush(); }
      if ((RELE_Status==0)&&(TEMPERATURE<=(SETPOINT-TEMP_HYSTERESIS_LO))) { RELE_Status = 1; Serial.println("RUNNING: thermostat rele ON."); Serial.flush(); }
   } else {
      // Disable rele actuation if temperature data is invalid
      RELE_Status = 0;
   }
   // Update RELE output state
   digitalWrite(RELE_Output, (RELE_Status==1 ? HIGH : LOW));
}
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------
void setup(void) {
   // Configure Serial port and speed
   delay(250);
   Serial.begin(115200);
   Serial.flush();
   Serial.print("."); Serial.flush(); delay(250);
   Serial.print("."); Serial.flush(); delay(250);
   Serial.print("."); Serial.flush(); delay(250);
   Serial.print("."); Serial.flush(); delay(250);
   Serial.println("."); Serial.flush(); delay(250);
   Serial.println(""); Serial.flush();
   Serial.print("CPU Freq......: "); Serial.print(ESP.getCpuFreqMHz()); Serial.println(" MHz");
   Serial.print("Total heap....: "); Serial.print(ESP.getHeapSize()); Serial.println(" bytes");
   Serial.print("Free heap.....: "); Serial.print(ESP.getFreeHeap()); Serial.println(" bytes");
   Serial.println("");
   Serial.flush();
   Serial.println("##############################################");
   Serial.println("###       ESP32 WiFi MQTT Thermostat       ###"); Serial.flush();
   Serial.println("##############################################");
   Serial.println("### Copyright by stefano Vailati           ###");
   Serial.printf ("### Release date: %10s               ###\n", OTA_CurrentDate);
   Serial.printf ("### FW ver.: %.02f                          ###\n", OTA_CurrentVersion);
   Serial.printf ("### FW size: %7d bytes                 ###\n", ESP.getSketchSize());
   Serial.println("##############################################");
   // Initialize RELE output pin
   Serial.println("INIT: initializing rele output..."); Serial.flush();
   pinMode(RELE_Output, OUTPUT);
   digitalWrite(RELE_Output, LOW);
   Serial.println("INIT: initializing rele output... DONE."); Serial.flush();
   // Initialize the BUZZER
   ledcSetup(0, 2000, 12);
   ledcAttachPin(BEEPER, 0);
   // Emit a BEEP at startup
   SOUND_Startup();
   // Initialize random number generator
   Serial.println("INIT: initializing random number generator..."); Serial.flush();
   randomSeed(analogRead(A0));
   Serial.println("INIT: initializing random number generator... DONE"); Serial.flush();
   // Initialize EEPROM with predefined size and read configuration data
   Serial.println("EEPROM: initializing user space area..."); Serial.flush();
   EEPROM.begin(EEPROM_SIZE);
   EEPROM_ReadSettings();
   Serial.println("EEPROM: initializing user space area... DONE."); Serial.flush();
   // Set the rotation before we calibrate
   Serial.println("INIT: initializing display..."); Serial.flush();
   pinMode(TFT_BL, OUTPUT);
   digitalWrite(TFT_BL, LOW);
   tft.init();
   tft.setRotation(3);
   //touch_calibrate();
   uint16_t calData[5] = { 410, 3445, 225, 3534, 1 };
   tft.setTouch(calData);   
   tft.fillScreen(TFT_BLUE);
   Serial.println("INIT: initializing display... DONE."); Serial.flush();
   // Start the I2C interface
   Serial.println("INIT: initializing I2C interface..."); Serial.flush();
   Wire.begin(I2C_Line_SDA,I2C_Line_SCL);
   Serial.println("INIT: initializing I2C interface... DONE."); Serial.flush();
   // Check RTC settings, and reset to a default date and time if invalid
   Serial.println("INIT: initializing RTC module..."); Serial.flush();
   bool h12, PM, Century=false;
   if ((Clock.getHour(h12, PM) < 0)||(Clock.getHour(h12, PM) > 23)||(Clock.getMinute() < 0)||(Clock.getMinute() > 59)||(Clock.getDoW() < 1)||(Clock.getDoW() > 7)) {
      // RTC error, configure the default date and time
      Serial.println("INIT: RTC data is invalid, resetting it..."); Serial.flush();
      Clock.setDoW(6); // 1-Domenica | 2-Lunedi | 3-Martedi | 4-Mercoledi | 5-Giovedi | 6-Venerdi | 7-Sabato
      Clock.setDate(27);
      Clock.setMonth(11);
      Clock.setYear(20);
      Clock.setHour(10);
      Clock.setMinute(18);
      Clock.setSecond(0);
   }
   RTC_ReadClock();
   Serial.println("INIT: initializing RTC module... DONE."); Serial.flush();
   // Initialize Si7021 sensor
   Serial.println("INIT: initializing Si7021 I2C sensor..."); Serial.flush();
   if (!si7021.begin()) { 
      Serial.println("INIT: initializing Si7021 I2C sensor... FAILED."); Serial.flush();
      SI7021_PRESENT = 0;
   } else {
      si7021.setHeatLevel(SI_HEATLEVEL_LOWEST or SI_HEATLEVEL_LOW);
      SI7021_PRESENT = 1;
   }
   Serial.println("INIT: initializing Si7021 I2C sensor... DONE."); Serial.flush();
   // Update ambient data
   AMBIENT_ReadData();
   // Obtain the full MacAddress to display infos and last 3 bytes for system name and mqtt topics
   String tmpMAC = "";
   WiFi.macAddress(MacAddress);
   for (int i = 3; i < 6; ++i) {
      if (MacAddress[i]<0x10) { tmpMAC += "0"; }
      tmpMAC += String(MacAddress[i],HEX);
   }
   tmpMAC.toUpperCase();
   String TMP_Name = String(OTA_SystemName) + "_";
   TMP_Name.concat(tmpMAC);
   ESP_Hostname = String(TMP_Name);
   MQTT_Topic = String(MQTT_BaseTopic) + tmpMAC;
   Serial.print("INIT: setting hostname to "); Serial.println(ESP_Hostname);
   // Update date and time from RTC device
   THERMOSTAT_Update();
   SCREEN_Layout();
   SCREEN_Data(true);
   // Start WIFI connection
   const char * NEW_Hostname = ESP_Hostname.c_str();
   WiFi.begin();
   WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
   WiFi.setHostname(NEW_Hostname);
   // Configure MQTT client settings
   mqttClient.setKeepAlive(MQTT_KeepAlive_TIMEOUT);
   mqttClient.setServer(MQTT_Server, 1883);
   mqttClient.setCallback(MQTT_CallBack);
   // Start a parallel loop handler for networking and mqtt activities
   Serial.println("INIT: starting Networking and MQTT parallel handler...");
   xTaskCreatePinnedToCore(NetMQTT_Management, "NetMQTT_Management", 4096, NULL, 0, &NetMQTT_Task, 1);
   Serial.println("INIT: starting Networking and MQTT parallel handler... DONE.");
   // Store current time at startup
   Serial.println("RUNNING: thermostat up and running."); Serial.flush();
   lastActivity = millis();
}
//------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------
void loop() {
   // Get current timing
   unsigned long currMillis = millis();
   // ######################################################
   // ### Cherck if there are any touch event to process ###
   // ######################################################
   uint16_t x, y;
   if (tft.getTouch(&x, &y)) {
      // Store last activity detection timer
      lastActivity = currMillis;
      // Check if touch event is the first or touch is kept pressed to avoid bouncing
      if (TOUCH_Down == 0) {
         // Activate the TOUCH-DOWN event flag
         TOUCH_Down = 1;
         TOUCH_DownStart = lastActivity;
         // Check TABS selections
         if ((x < 43)&&(y >=   0)&&(y <=  47)&&(MENU_Section!=0)) { SOUND_KeyPress(); MENU_Section=0; displayUpdate=1; SCREEN_Layout(); }
         if ((x < 43)&&(y >=  48)&&(y <=  95)&&(MENU_Section!=1)) { SOUND_KeyPress(); MENU_Section=1; displayUpdate=1; SCREEN_Layout(); }
         if ((x < 43)&&(y >=  96)&&(y <= 143)&&(MENU_Section!=2)) { SOUND_KeyPress(); MENU_Section=2; displayUpdate=1; SCREEN_Layout(); }
         if ((x < 43)&&(y >= 144)&&(y <= 191)&&(MENU_Section!=3)) { SOUND_KeyPress(); MENU_Section=3; displayUpdate=1; SCREEN_Layout(); SETTINGS_PositionX=0; SETTINGS_PositionY=0; }
         if ((x < 43)&&(y >= 192)&&(y <= 239)&&(MENU_Section!=4)) { SOUND_KeyPress(); MENU_Section=4; displayUpdate=1; SCREEN_Layout(); SETTINGS_PositionX=0; }
         // Check Menu-HOME selections
         // Check Menu-MODE selections
         if ((MENU_Section==1)&&(x >= 91)&&(x <= 273)&&(y >=  41)&&(y <=  78)) { SOUND_ButtonActivate(); THERMO_Mode=0; displayUpdate=1; MENU_Section=0; SCREEN_Layout(); }
         if ((MENU_Section==1)&&(x >= 91)&&(x <= 273)&&(y >=  91)&&(y <= 128)) { SOUND_ButtonActivate(); THERMO_Mode=1; displayUpdate=1; MENU_Section=0; SCREEN_Layout(); }
         if ((MENU_Section==1)&&(x >= 91)&&(x <= 273)&&(y >= 141)&&(y <= 178)) { SOUND_ButtonActivate(); THERMO_Mode=2; displayUpdate=1; MENU_Section=0; SCREEN_Layout(); }
         if ((MENU_Section==1)&&(x >= 91)&&(x <= 273)&&(y >= 191)&&(y <= 228)) { SOUND_ButtonActivate(); THERMO_Mode=3; displayUpdate=1; MENU_Section=0; SCREEN_Layout(); }
         // Check Menu-SETPOINT selections
         if ((MENU_Section==2)&&(x >= 214)&&(x <= 249)&&(y >=  34)&&(y <=  69)&&(THERMO_TM<TEMP_ON_MAX)) { SOUND_KeyPress(); THERMO_TM+=.5f; displayUpdate=1; }
         if ((MENU_Section==2)&&(x >= 274)&&(x <= 309)&&(y >=  34)&&(y <=  69)&&(THERMO_TM>TEMP_ON_MIN)) { SOUND_KeyPress(); THERMO_TM-=.5f; displayUpdate=1; }
         if ((MENU_Section==2)&&(x >= 214)&&(x <= 249)&&(y >=  89)&&(y <= 124)&&(THERMO_T2<TEMP_ON_MAX)) { SOUND_KeyPress(); THERMO_T2+=.5f; displayUpdate=1; }
         if ((MENU_Section==2)&&(x >= 274)&&(x <= 309)&&(y >=  89)&&(y <= 124)&&(THERMO_T2>TEMP_ON_MIN)) { SOUND_KeyPress(); THERMO_T2-=.5f; displayUpdate=1; }
         if ((MENU_Section==2)&&(x >= 214)&&(x <= 249)&&(y >= 144)&&(y <= 179)&&(THERMO_T1<TEMP_ON_MAX)) { SOUND_KeyPress(); THERMO_T1+=.5f; displayUpdate=1; }
         if ((MENU_Section==2)&&(x >= 274)&&(x <= 309)&&(y >= 144)&&(y <= 179)&&(THERMO_T1>TEMP_ON_MIN)) { SOUND_KeyPress(); THERMO_T1-=.5f; displayUpdate=1; }
         if ((MENU_Section==2)&&(x >= 214)&&(x <= 249)&&(y >= 199)&&(y <= 234)&&(THERMO_T0<TEMP_OFF_MAX)) { SOUND_KeyPress(); THERMO_T0+=.5f; displayUpdate=1; }
         if ((MENU_Section==2)&&(x >= 274)&&(x <= 309)&&(y >= 199)&&(y <= 234)&&(THERMO_T0>TEMP_OFF_MIN)) { SOUND_KeyPress(); THERMO_T0-=.5f; displayUpdate=1; }
         // Check Menu-SCHEDULE selections
         if ((MENU_Section==3)&&(x >= 229)&&(x <= 299)&&(y >= 36)&&(y <= 71)) { 
            SOUND_KeyPress();
            switch (SETTINGS_PositionY) {
               case 0: for (int iii=0; iii<24; iii++) { THERMO_Schedule_1[iii] = DEF_WEEK[iii]; } break;
               case 1: for (int iii=0; iii<24; iii++) { THERMO_Schedule_2[iii] = DEF_WEEK[iii]; } break;
               case 2: for (int iii=0; iii<24; iii++) { THERMO_Schedule_3[iii] = DEF_WEEK[iii]; } break;
               case 3: for (int iii=0; iii<24; iii++) { THERMO_Schedule_4[iii] = DEF_WEEK[iii]; } break;
               case 4: for (int iii=0; iii<24; iii++) { THERMO_Schedule_5[iii] = DEF_WEEK[iii]; } break;
               case 5: for (int iii=0; iii<24; iii++) { THERMO_Schedule_6[iii] = DEF_WEEKEND[iii]; } break;
               case 6: for (int iii=0; iii<24; iii++) { THERMO_Schedule_7[iii] = DEF_WEEKEND[iii]; } break;
               case 7: for (int iii=0; iii<24; iii++) { THERMO_Schedule_H[iii] = DEF_WEEKEND[iii]; } break;
            }
            THERMO_Schedule_CHANGE=1;
            displayUpdate=1;
         }
         if ((MENU_Section==3)&&(x >=  74)&&(x <= 109)&&(y >= 141)&&(y <= 176)&&(SETTINGS_PositionY<7)) { SOUND_KeyPress(); SETTINGS_PositionY+=1; SETTINGS_PositionX=0; displayUpdate=1; }
         if ((MENU_Section==3)&&(x >=  74)&&(x <= 109)&&(y >= 191)&&(y <= 226)&&(SETTINGS_PositionY>0)) { SOUND_KeyPress(); SETTINGS_PositionY-=1; SETTINGS_PositionX=0; displayUpdate=1; }
         if ((MENU_Section==3)&&(x >= 139)&&(x <= 174)&&(y >= 166)&&(y <= 201)&&(SETTINGS_PositionX>0)) { SOUND_KeyPress(); SETTINGS_PositionX-=1; displayUpdate=1; }
         if ((MENU_Section==3)&&(x >= 199)&&(x <= 234)&&(y >= 166)&&(y <= 201)&&(SETTINGS_PositionX<23)) { SOUND_KeyPress(); SETTINGS_PositionX+=1; displayUpdate=1; }
         if ((MENU_Section==3)&&(x >= 264)&&(x <= 299)&&(y >= 141)&&(y <= 176)) {
            SOUND_KeyPress();  
            switch (SETTINGS_PositionY) {
               case 0: THERMO_Schedule_1[SETTINGS_PositionX] = 2; break;
               case 1: THERMO_Schedule_2[SETTINGS_PositionX] = 2; break;
               case 2: THERMO_Schedule_3[SETTINGS_PositionX] = 2; break;
               case 3: THERMO_Schedule_4[SETTINGS_PositionX] = 2; break;
               case 4: THERMO_Schedule_5[SETTINGS_PositionX] = 2; break;
               case 5: THERMO_Schedule_6[SETTINGS_PositionX] = 2; break;
               case 6: THERMO_Schedule_7[SETTINGS_PositionX] = 2; break;
               case 7: THERMO_Schedule_H[SETTINGS_PositionX] = 2; break;
            }
            THERMO_Schedule_CHANGE=1;
            displayUpdate=1;
         }
         if ((MENU_Section==3)&&(x >= 264)&&(x <= 299)&&(y >= 191)&&(y <= 226)) {
            SOUND_KeyPress(); 
            switch (SETTINGS_PositionY) {
               case 0: THERMO_Schedule_1[SETTINGS_PositionX] = 1; break;
               case 1: THERMO_Schedule_2[SETTINGS_PositionX] = 1; break;
               case 2: THERMO_Schedule_3[SETTINGS_PositionX] = 1; break;
               case 3: THERMO_Schedule_4[SETTINGS_PositionX] = 1; break;
               case 4: THERMO_Schedule_5[SETTINGS_PositionX] = 1; break;
               case 5: THERMO_Schedule_6[SETTINGS_PositionX] = 1; break;
               case 6: THERMO_Schedule_7[SETTINGS_PositionX] = 1; break;
               case 7: THERMO_Schedule_H[SETTINGS_PositionX] = 1; break;
            }
            THERMO_Schedule_CHANGE=1;
            displayUpdate=1;
         }
         // Check Menu-SETTINGS selections
         if ((MENU_Section==4)&&(x >= 224)&&(x <= 303)&&(y >= 36)&&(y <= 67)) { SOUND_ButtonActivate(); MENU_Section=0; SCREEN_Layout(); displayUpdate=1; forceWPSpairing=1; }
         if ((MENU_Section==4)&&(x >= 224)&&(x <= 303)&&(y >= 82)&&(y <= 113)) { SOUND_ButtonActivate(); WiFi.disconnect(false,true); MENU_Section=0; SCREEN_Layout(); displayUpdate=1; }
         if ((MENU_Section==4)&&(x >= 224)&&(x <= 303)&&(y >= 128)&&(y <= 159)) { SOUND_ButtonActivate(); MENU_Section=0; SCREEN_Layout(); displayUpdate=1; forceFirmwareUpdate=1; }
         if ((MENU_Section==4)&&(x >=  74)&&(x <= 109)&&(y >= 191)&&(y <= 226)) { 
            SOUND_KeyPress(); 
            if (SETTINGS_PositionX>0) {
               SETTINGS_PositionX -= 1; 
            } else {
               SETTINGS_PositionX = 11;
            }
            displayUpdate=1;
         }
         if ((MENU_Section==4)&&(x >= 134)&&(x <= 169)&&(y >= 191)&&(y <= 226)) {
            SOUND_KeyPress(); 
            SETTINGS_PositionX+=1;
            if (SETTINGS_PositionX>11) { SETTINGS_PositionX = 0; }
            displayUpdate=1;
         }
         if ((MENU_Section==4)&&(x >= 204)&&(x <= 239)&&(y >= 191)&&(y <= 226)) {
            // Increase selected value
            SOUND_KeyPress(); 
            bool h12, PM, Century=false;
            int tmpVL, tmpVL2;
            switch (SETTINGS_PositionX) {
               case 0:
                  // Change HOURS value
                  tmpVL = Clock.getHour(h12, PM);
                  tmpVL += 1;
                  if (tmpVL > 23) { tmpVL = 0; }
                  Clock.setHour(tmpVL);
                  break;
               case 1:
                  // Change MINUTE value
                  tmpVL = Clock.getMinute();
                  tmpVL += 1;
                  if (tmpVL > 59) { tmpVL = 0; }
                  Clock.setMinute(tmpVL);
                  Clock.setSecond(0);
                  break;
               case 2:
                  // Change DAY-OF-WEEK value
                  tmpVL = Clock.getDoW();
                  tmpVL += 1;
                  if (tmpVL > 7) { tmpVL = 1; }
                  Clock.setDoW(tmpVL);
                  break;
               case 3:
                  // Change DAY value
                  tmpVL2 = Clock.getMonth(Century);
                  tmpVL = Clock.getDate();
                  tmpVL += 1;
                  if ((tmpVL2==2)&&(tmpVL>29)) { tmpVL = 1; }
                  if (((tmpVL2==4)||(tmpVL2==6)||(tmpVL2==9)||(tmpVL2==11))&&(tmpVL>30)) { tmpVL = 1; }
                  if (tmpVL > 31) { tmpVL = 1; }
                  Clock.setDate(tmpVL);
                  break;
               case 4:
                  // Change MONTH value
                  tmpVL = Clock.getMonth(Century);
                  tmpVL += 1;
                  if (tmpVL > 12) { tmpVL = 1; }
                  Clock.setMonth(tmpVL);
                  break;
               case 5:
                  // Change YEAR value
                  tmpVL = Clock.getYear();
                  tmpVL += 1;
                  if (tmpVL > 99) { tmpVL = 0; }
                  Clock.setYear(tmpVL);
                  break;
               case 6:
                  MQTT_Server[0] += 1;
                  break;   
               case 7:
                  MQTT_Server[1] += 1;
                  break;   
               case 8:
                  MQTT_Server[2] += 1;
                  break;   
               case 9:
                  MQTT_Server[3] += 1;
                  break;   
               case 10:
                  if (TEMP_GAIN < TEMP_GAIN_MAX) {
                     TEMP_GAIN += 1;
                     AMBIENT_ReadData();
                  }
                  break;    
               case 11:
                  if (HUMI_GAIN < HUMI_GAIN_MAX) {
                     HUMI_GAIN += 1;
                     AMBIENT_ReadData();
                  }
                  break;    
            }
            displayUpdate=1;
         }
         if ((MENU_Section==4)&&(x >= 264)&&(x <= 299)&&(y >= 191)&&(y <= 226)) {
            // Decrease selected value
            SOUND_KeyPress(); 
            bool h12, PM, Century=false;
            int tmpVL, tmpVL2;
            switch (SETTINGS_PositionX) {
               case 0:
                  // Change HOURS value
                  tmpVL = Clock.getHour(h12, PM);
                  tmpVL -= 1;
                  if (tmpVL < 0) { tmpVL = 23; }
                  Clock.setHour(tmpVL);
                  break;
               case 1:
                  tmpVL = Clock.getMinute();
                  tmpVL -= 1;
                  if (tmpVL < 0) { tmpVL = 59; }
                  Clock.setMinute(tmpVL);
                  Clock.setSecond(0);
                  break;
               case 2:
                  // Change DAY-OF-WEEK value
                  tmpVL = Clock.getDoW();
                  tmpVL -= 1;
                  if (tmpVL < 1) { tmpVL = 7; }
                  Clock.setDoW(tmpVL);
                  break;
               case 3:
                  // Change DAY value
                  tmpVL2 = Clock.getMonth(Century);
                  tmpVL = Clock.getDate();
                  tmpVL -= 1;
                  if ((tmpVL2==2)&&(tmpVL<1)) { tmpVL = 29; }
                  if (((tmpVL2==4)||(tmpVL2==6)||(tmpVL2==9)||(tmpVL2==11))&&(tmpVL<1)) { tmpVL = 30; }
                  if (tmpVL < 1) { tmpVL = 31; }
                  Clock.setDate(tmpVL);
                  break;
               case 4:
                  // Change MONTH value
                  tmpVL = Clock.getMonth(Century);
                  tmpVL -= 1;
                  if (tmpVL < 1) { tmpVL = 12; }
                  Clock.setMonth(tmpVL);
                  break;
               case 5:
                  // Change YEAR value
                  tmpVL = Clock.getYear();
                  tmpVL -= 1;
                  if (tmpVL < 0) { tmpVL = 99; }
                  Clock.setYear(tmpVL);
                  break;
               case 6:
                  MQTT_Server[0] -= 1;
                  break;   
               case 7:
                  MQTT_Server[1] -= 1;
                  break;   
               case 8:
                  MQTT_Server[2] -= 1;
                  break;   
               case 9:
                  MQTT_Server[3] -= 1;
                  break;  
               case 10:
                  if (TEMP_GAIN > TEMP_GAIN_MIN) {
                     TEMP_GAIN -= 1;
                     AMBIENT_ReadData();
                  }
                  break;    
               case 11:
                  if (HUMI_GAIN > HUMI_GAIN_MIN) {
                     HUMI_GAIN -= 1;
                     AMBIENT_ReadData();
                  }
                  break;    
            }
            displayUpdate=1;
         }
      } else {
         // Touch is being kept pressed, repeat action automatically every "TOUCH_DownKeptDelay" seconds
         if ((unsigned long)(lastActivity - TOUCH_DownStart)/1000 > TOUCH_DownKeptDelay) {
            // Reset TOUCH_DownStart value in order to repeat action at interval of 250 ms
            TOUCH_DownStart += (unsigned long)(TOUCH_DownKeptDelayRepeat*1000);
            // Check Menu-SETPOINT selections
            if ((MENU_Section==2)&&(x >= 214)&&(x <= 249)&&(y >=  34)&&(y <=  69)&&(THERMO_TM<TEMP_ON_MAX)) { SOUND_KeyPress(); THERMO_TM+=.5f; displayUpdate=1; }
            if ((MENU_Section==2)&&(x >= 274)&&(x <= 309)&&(y >=  34)&&(y <=  69)&&(THERMO_TM>TEMP_ON_MIN)) { SOUND_KeyPress(); THERMO_TM-=.5f; displayUpdate=1; }
            if ((MENU_Section==2)&&(x >= 214)&&(x <= 249)&&(y >=  89)&&(y <= 124)&&(THERMO_T2<TEMP_ON_MAX)) { SOUND_KeyPress(); THERMO_T2+=.5f; displayUpdate=1; }
            if ((MENU_Section==2)&&(x >= 274)&&(x <= 309)&&(y >=  89)&&(y <= 124)&&(THERMO_T2>TEMP_ON_MIN)) { SOUND_KeyPress(); THERMO_T2-=.5f; displayUpdate=1; }
            if ((MENU_Section==2)&&(x >= 214)&&(x <= 249)&&(y >= 144)&&(y <= 179)&&(THERMO_T1<TEMP_ON_MAX)) { SOUND_KeyPress(); THERMO_T1+=.5f; displayUpdate=1; }
            if ((MENU_Section==2)&&(x >= 274)&&(x <= 309)&&(y >= 144)&&(y <= 179)&&(THERMO_T1>TEMP_ON_MIN)) { SOUND_KeyPress(); THERMO_T1-=.5f; displayUpdate=1; }
            if ((MENU_Section==2)&&(x >= 214)&&(x <= 249)&&(y >= 199)&&(y <= 234)&&(THERMO_T0<TEMP_OFF_MAX)) { SOUND_KeyPress(); THERMO_T0+=.5f; displayUpdate=1; }
            if ((MENU_Section==2)&&(x >= 274)&&(x <= 309)&&(y >= 199)&&(y <= 234)&&(THERMO_T0>TEMP_OFF_MIN)) { SOUND_KeyPress(); THERMO_T0-=.5f; displayUpdate=1; }
            // Check Menu-SCHEDULE selections
            if ((MENU_Section==3)&&(x >=  74)&&(x <= 109)&&(y >= 141)&&(y <= 176)&&(SETTINGS_PositionY<7)) { SOUND_KeyPress(); SETTINGS_PositionY+=1; SETTINGS_PositionX=0; displayUpdate=1; }
            if ((MENU_Section==3)&&(x >=  74)&&(x <= 109)&&(y >= 191)&&(y <= 226)&&(SETTINGS_PositionY>0)) { SOUND_KeyPress(); SETTINGS_PositionY-=1; SETTINGS_PositionX=0; displayUpdate=1; }
            if ((MENU_Section==3)&&(x >= 139)&&(x <= 174)&&(y >= 166)&&(y <= 201)&&(SETTINGS_PositionX>0)) { SOUND_KeyPress(); SETTINGS_PositionX-=1; displayUpdate=1; }
            if ((MENU_Section==3)&&(x >= 199)&&(x <= 234)&&(y >= 166)&&(y <= 201)&&(SETTINGS_PositionX<23)) { SOUND_KeyPress(); SETTINGS_PositionX+=1; displayUpdate=1; }
            // Check Menu-SETTINGS selections
            if ((MENU_Section==4)&&(x >= 204)&&(x <= 239)&&(y >= 191)&&(y <= 226)) {
               // Increase selected value
               SOUND_KeyPress(); 
               bool h12, PM, Century=false;
               int tmpVL, tmpVL2;
               switch (SETTINGS_PositionX) {
                  case 0:
                     // Change HOURS value
                     tmpVL = Clock.getHour(h12, PM);
                     tmpVL += 1;
                     if (tmpVL > 23) { tmpVL = 0; }
                     Clock.setHour(tmpVL);
                     break;
                  case 1:
                     // Change MINUTE value
                     tmpVL = Clock.getMinute();
                     tmpVL += 1;
                     if (tmpVL > 59) { tmpVL = 0; }
                     Clock.setMinute(tmpVL);
                     Clock.setSecond(0);
                     break;
                  case 2:
                     // Change DAY-OF-WEEK value
                     tmpVL = Clock.getDoW();
                     tmpVL += 1;
                     if (tmpVL > 7) { tmpVL = 1; }
                     Clock.setDoW(tmpVL);
                     break;
                  case 3:
                     // Change DAY value
                     tmpVL2 = Clock.getMonth(Century);
                     tmpVL = Clock.getDate();
                     tmpVL += 1;
                     if ((tmpVL2==2)&&(tmpVL>29)) { tmpVL = 1; }
                     if (((tmpVL2==4)||(tmpVL2==6)||(tmpVL2==9)||(tmpVL2==11))&&(tmpVL>30)) { tmpVL = 1; }
                     if (tmpVL > 31) { tmpVL = 1; }
                     Clock.setDate(tmpVL);
                     break;
                  case 4:
                     // Change MONTH value
                     tmpVL = Clock.getMonth(Century);
                     tmpVL += 1;
                     if (tmpVL > 12) { tmpVL = 1; }
                     Clock.setMonth(tmpVL);
                     break;
                  case 5:
                     // Change YEAR value
                     tmpVL = Clock.getYear();
                     tmpVL += 1;
                     if (tmpVL > 99) { tmpVL = 0; }
                     Clock.setYear(tmpVL);
                     break;
                  case 6:
                     MQTT_Server[0] += 1;
                     break;   
                  case 7:
                     MQTT_Server[1] += 1;
                     break;   
                  case 8:
                     MQTT_Server[2] += 1;
                     break;   
                  case 9:
                     MQTT_Server[3] += 1;
                     break;   
                  case 10:
                     if (TEMP_GAIN < TEMP_GAIN_MAX) {
                        TEMP_GAIN += 1;
                        AMBIENT_ReadData();
                     }
                     break;    
                  case 11:
                     if (HUMI_GAIN < HUMI_GAIN_MAX) {
                        HUMI_GAIN += 1;
                        AMBIENT_ReadData();
                     }
                     break;    
               }
               displayUpdate=1;
            }
            if ((MENU_Section==4)&&(x >= 264)&&(x <= 299)&&(y >= 191)&&(y <= 226)) {
               // Decrease selected value
               SOUND_KeyPress(); 
               bool h12, PM, Century=false;
               int tmpVL, tmpVL2;
               switch (SETTINGS_PositionX) {
                  case 0:
                     // Change HOURS value
                     tmpVL = Clock.getHour(h12, PM);
                     tmpVL -= 1;
                     if (tmpVL < 0) { tmpVL = 23; }
                     Clock.setHour(tmpVL);
                     break;
                  case 1:
                     tmpVL = Clock.getMinute();
                     tmpVL -= 1;
                     if (tmpVL < 0) { tmpVL = 59; }
                     Clock.setMinute(tmpVL);
                     Clock.setSecond(0);
                     break;
                  case 2:
                     // Change DAY-OF-WEEK value
                     tmpVL = Clock.getDoW();
                     tmpVL -= 1;
                     if (tmpVL < 1) { tmpVL = 7; }
                     Clock.setDoW(tmpVL);
                     break;
                  case 3:
                     // Change DAY value
                     tmpVL2 = Clock.getMonth(Century);
                     tmpVL = Clock.getDate();
                     tmpVL -= 1;
                     if ((tmpVL2==2)&&(tmpVL<1)) { tmpVL = 29; }
                     if (((tmpVL2==4)||(tmpVL2==6)||(tmpVL2==9)||(tmpVL2==11))&&(tmpVL<1)) { tmpVL = 30; }
                     if (tmpVL < 1) { tmpVL = 31; }
                     Clock.setDate(tmpVL);
                     break;
                  case 4:
                     // Change MONTH value
                     tmpVL = Clock.getMonth(Century);
                     tmpVL -= 1;
                     if (tmpVL < 1) { tmpVL = 12; }
                     Clock.setMonth(tmpVL);
                     break;
                  case 5:
                     // Change YEAR value
                     tmpVL = Clock.getYear();
                     tmpVL -= 1;
                     if (tmpVL < 0) { tmpVL = 99; }
                     Clock.setYear(tmpVL);
                     break;
                  case 6:
                     MQTT_Server[0] -= 1;
                     break;   
                  case 7:
                     MQTT_Server[1] -= 1;
                     break;   
                  case 8:
                     MQTT_Server[2] -= 1;
                     break;   
                  case 9:
                     MQTT_Server[3] -= 1;
                     break;  
                  case 10:
                     if (TEMP_GAIN > TEMP_GAIN_MIN) {
                        TEMP_GAIN -= 1;
                        AMBIENT_ReadData();
                     }
                     break;    
                  case 11:
                     if (HUMI_GAIN > HUMI_GAIN_MIN) {
                        HUMI_GAIN -= 1;
                        AMBIENT_ReadData();
                     }
                     break;    
               }
               displayUpdate=1;
            }
         }
      }
   } else {
      // Reset the TOUCH-DOWN event flag if active
      if (TOUCH_Down == 1) { TOUCH_Down=0; }
   }
   // Keep MQTT connection status active
   mqttClient.loop();
   // ###############################################################################
   // ### Check if read environment data (TEMPERATURE / HUMIDITY) every 5 seconds ###
   // ###############################################################################
   if ((unsigned long)(currMillis - lastEnvRead)/1000 > ENVIRONMENT_ReadInterval) {
      // Update current temperature
      AMBIENT_ReadData();
      // Store last display refresh
      lastEnvRead = currMillis;
   }
   // ###############################################################################
   // ### Return to HOME screen from other screen after inactivity timeout occurs ###
   // ###############################################################################
   if ((((unsigned long)(currMillis - lastActivity)/1000 > DISPLAY_BackToHomeTimeout)&&( MENU_Section != 0))||(displayUpdate==2)) {
      // Reset current screen to HOME
      MENU_Section=0;
      // Force a display complete update
      displayUpdate=1;
      // Refresh screen layout
      SCREEN_Layout();
   }
   // ###########################################################
   // ### Update the screen display if requested by something ###
   // ###########################################################
   if ((displayUpdate == 1)||((unsigned long)(currMillis - lastRefresh)/1000 > DISPLAY_RefreshRate)) {
      // Update current date and time from RTC device
      RTC_ReadClock();
      // Check thermostat work mode and rele
      THERMOSTAT_Update();
      // Force send an MQTT status update if requested by a display force update
      if ( displayUpdate==1 ) { MQTT_SendStatus(); }
      // Store settings if changes are present
      EEPROM_WriteSettings();
      // Update the display
      SCREEN_Data(displayUpdate==1);
      // Reset the display update flag and store last update time
      displayUpdate = 0;
      lastRefresh = currMillis;
   }
   // ########################################################################################
   // ### Check every 10 minutes for Firmware Updates availability and manage installation ###
   // ########################################################################################
   if ((((unsigned long)(currMillis - lastFirmwareCheck) >= (OTA_UpdatesInterval * 1000))||(forceFirmwareUpdate==1))&&(MENU_Section==0)) { 
      // Reset the firmware update force flag
      forceFirmwareUpdate = 0;
      // Store last time for next interval check
      lastFirmwareCheck = currMillis;
      // Activate update indicator during update check
      UPDT_Status = 1;
      // Check for firmware updates
      checkFirmwareUpdates();
      // Deactivate update indicator when update check finished
      UPDT_Status = 0;
      // Reset display and menu settings to force a full screen refresh
      MENU_Section_PREV = 255;
   }
  // Introduce a delay for 100 milliseconds
   delay(100);
}
