//#define Xconfiguration
#ifndef configuration_hhh
#define configuration_hhh

int baudRateSerial = 115200;

char mqtt_server[40] = "192.168.0.55";
char mqtt_port[6] = "1883";
char idInstall[40] ="";
char idEquipment[40] ="";
char idSensorPluviometer[40] ="";
char idSensorAnemometer[40] ="";
char idSensorVane[40] ="";

String idESP8266;

//flag for saving data
bool shouldSaveConfig = false;

String channelFormat;
char channel_char[100]; 
String channel;


/**
 * Configurações de Pins
 */
const byte WSPEED = 14;
const byte RAIN = 12;

// analog I/O pins
const byte WDIR = A0;

//{Anemometer, Pluviometer, Vane,...};
const bool flagArray[] = {true, true, true};

//{Anemometer, Pluviometer, Vane,...};
const int pinArray[] = {12, 14, A0};

//{Anemometer, Pluviometer, Vane,...};
const int updateTimeArray[] = {10000, 10000, 10000};

//{Anemometer, Pluviometer, Vane,...};
long lastUpdateTimeArray[] = {0, 0, 0};


#define SENS_AGR_VANE_E     0
#define SENS_AGR_VANE_ENE   1
#define SENS_AGR_VANE_NE    2
#define SENS_AGR_VANE_NNE   3
#define SENS_AGR_VANE_N     4
#define SENS_AGR_VANE_NNW   5
#define SENS_AGR_VANE_NW    6
#define SENS_AGR_VANE_WNW   7
#define SENS_AGR_VANE_W     8
#define SENS_AGR_VANE_WSW   9
#define SENS_AGR_VANE_SW    10
#define SENS_AGR_VANE_SSW   11
#define SENS_AGR_VANE_S     12
#define SENS_AGR_VANE_SSE   13
#define SENS_AGR_VANE_SE    14
#define SENS_AGR_VANE_ESE   15

#endif
