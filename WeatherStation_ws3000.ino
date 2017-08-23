#include <FS.h> //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <ArduinoOTA.h>

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include "Configuration.h"

WiFiClient espClient;
PubSubClient client(espClient);

char msg[50];

char result[200];

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

//Global Variables
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
long lastSecond; //The millis counter to see when a second rolls by
byte seconds; //When it hits 60, increase the current minute
byte seconds_2m; //Keeps track of the "wind speed/dir avg" over last 2 minutes array of data
byte minutes; //Keeps track of where we are in various arrays of data
byte minutes_10m; //Keeps track of where we are in wind gust/dir over last 10 minutes array of data

long lastWindCheck = 0;
volatile long lastWindIRQ = 0;
volatile byte windClicks = 0;

//We need to keep track of the following variables:
//Wind speed/dir each update (no storage)
//Wind gust/dir over the day (no storage)
//Wind speed/dir, avg over 2 minutes (store 1 per second)
//Wind gust/dir over last 10 minutes (store 1 per minute)
//Rain over the past hour (store 1 per minute)
//Total rain over date (store one per day)

byte windspdavg[120]; //120 bytes to keep track of 2 minute average

#define WIND_DIR_AVG_SIZE 120
int winddiravg[WIND_DIR_AVG_SIZE]; //120 ints to keep track of 2 minute average
float windgust_10m[10]; //10 floats to keep track of 10 minute max
int windgustdirection_10m[10]; //10 ints to keep track of 10 minute max
volatile float rainHour[60]; //60 floating numbers to keep track of 60 minutes of rain

//These are all the weather values that wunderground expects:
int winddir = 0; // [0-360 instantaneous wind direction]
float windspeedkmh = 0; // [kmh instantaneous wind speed]
float windgustkmh = 0; // [kmh current wind gust, using software specific time period]
int windgustdir = 0; // [0-360 using software specific time period]
float windspdkmh_avg2m = 0; // [mph 2 minute average wind speed mph]
int winddir_avg2m = 0; // [0-360 2 minute average wind direction]
float windgustkmh_10m = 0; // [kmh past 10 minutes wind gust mph ]
int windgustdir_10m = 0; // [0-360 past 10 minutes wind gust direction]
float rainin = 0; // [rain inches over the past hour)] -- the accumulated rainfall in the past 60 min
volatile float dailyrainin = 0; // [rain inches so far today in local time]

// volatiles are subject to modification by IRQs
volatile unsigned long raintime, rainlast, raininterval, rain;

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

//Interrupt routines (these are called by the hardware interrupts, not by the main code)
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void rainIRQ(){
// Count rain gauge bucket tips as they occur
// Activated by the magnet and reed switch in the rain gauge, attached to input D2
    raintime = millis(); // grab current time
    raininterval = raintime - rainlast; // calculate interval between this and last event

    if (raininterval > 10) { // ignore switch-bounce glitches less than 10mS after initial edge
        //0.011” (0.2794 mm)
        dailyrainin += 0.011 * 25.4; //Each dump is 0.011" of water(0.2794 mm)
        rainHour[minutes] += 0.011 + 25.4; //Increase this minute's amount of rain(0.2794 mm)
        //rainHour[minutes] +=0.011” (0.2794 mm)
        rainlast = raintime; // set up for next event
    }
}

void wspeedIRQ(){
// Activated by the magnet in the anemometer (2 ticks per rotation), attached to input D3
    if (millis() - lastWindIRQ > 10){ // Ignore switch-bounce glitches less than 10ms (142MPH max reading) after the reed switch closes
        lastWindIRQ = millis(); //Grab the current time
        windClicks++; //There is 1.492MPH for each click per second.
    }
}


//callback notifying us of the need to save config
void saveConfigCallback(){
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

void setup(){
  // put your setup code here, to run once:
  Serial.begin(baudRateSerial);
  Serial.println();
    //clean FS, for testing
    //SPIFFS.format();

    //read configuration from FS json
    Serial.println("mounting FS...");

    if (SPIFFS.begin()) {
        Serial.println("mounted file system");
        if (SPIFFS.exists("/config.json")) {
            //file exists, reading and loading
            Serial.println("reading config file");
            File
            configFile = SPIFFS.open("/config.json", "r");
            if (configFile) {
                Serial.println("opened config file");
                size_t
                size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr < char[] > buf(new char[size]);

                configFile.readBytes(buf.get(), size);
                DynamicJsonBuffer jsonBuffer;
                JsonObject & json = jsonBuffer.parseObject(buf.get());
                json.printTo(Serial);
                if (json.success()) {
                    Serial.println("\nparsed json");

                    strcpy(mqtt_server, json["mqtt_server"]);
                    strcpy(mqtt_port, json["mqtt_port"]);

                    strcpy(idInstall, json["idInstall"]);
                    strcpy(idEquipment, json["idEquipment"]);
                    strcpy(idSensorPluviometer, json["idSensorPluviometer"]);
                    strcpy(idSensorAnemometer, json["idSensorAnemometer"]);
                    strcpy(idSensorVane, json["idSensorVane"]);

                } else {
                    Serial.println("failed to load json config");
                }
            }
        }
    } else {
        Serial.println("failed to mount FS");
    }

    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);

    
    WiFiManagerParameter custom_idInstall("idInstall", "ID Install", idInstall, 40);
    WiFiManagerParameter custom_idEquipment("idEquipment", "ID Equipment", idEquipment, 40);

    WiFiManagerParameter custom_idSensorPluviometer("idSensorPluviometer", "ID Sensor Pluviometer", idSensorPluviometer, 40);
    WiFiManagerParameter custom_idSensorAnemometer("idSensorAnemometer", "ID Sensor Anemometer", idSensorAnemometer, 40);
    WiFiManagerParameter custom_idSensorVane("idSensorVane", "ID Sensor Vane", idSensorVane, 40);

    //WiFiManager
    WiFiManager wifiManager;

    //set config save notify callback
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    //add all your parameters here
    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_idInstall);
    wifiManager.addParameter(&custom_idEquipment);
    wifiManager.addParameter(&custom_idSensorPluviometer);
    wifiManager.addParameter(&custom_idSensorAnemometer);
    wifiManager.addParameter(&custom_idSensorVane);
    

    //reset settings - for testing
    //wifiManager.resetSettings();

    //set minimu quality of signal so it ignores AP's under that quality - defaults to 8%
    //wifiManager.setMinimumSignalQuality();

    //sets timeout until configuration portal gets turned off - useful to make it all retry or go to sleep - in seconds
    //wifiManager.setTimeout(120);

    if (!wifiManager.autoConnect()) {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again, or maybe put it to deep sleep
        ESP.reset();
        delay(5000);
    }

    //read updated parameters
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());

    strcpy(idInstall, custom_idInstall.getValue());
    strcpy(idEquipment, custom_idEquipment.getValue());
    strcpy(idSensorPluviometer, custom_idSensorPluviometer.getValue());
    strcpy(idSensorAnemometer, custom_idSensorAnemometer.getValue());
    strcpy(idSensorVane, custom_idSensorVane.getValue());

    //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;

    json["idInstall"] = idInstall;
    json["idEquipment"] = idEquipment;
    json["idSensorPluviometer"] = idSensorPluviometer;
    json["idSensorAnemometer"] = idSensorAnemometer;
    json["idSensorVane"] = idSensorVane;

  
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

    ArduinoOTA.onStart([](){
        Serial.println("Start");
    });
    ArduinoOTA.onEnd([](){
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error){
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    /*<idInstall>/<idEquipment>/<idSensor>/<#number>/<read/write>;*/
    channelFormat = (String(idInstall) + "/" + String(idEquipment) + "/%s/%d/%s");

    idESP8266 = "ESP" + String(ESP.getChipId());
    Serial.print("idESP8266: ");
    Serial.println(idESP8266);  
    Serial.print("Configuring access point...");

    setup_MQTT();

    
    pinMode(WSPEED, INPUT_PULLUP); // input from wind meters windspeed sensor
    pinMode(RAIN, INPUT_PULLUP); // input from wind meters rain gauge sensor

    seconds = 0;
    lastSecond = millis();

    // attach external interrupt pins to IRQ functions
    attachInterrupt(WSPEED, wspeedIRQ, FALLING);
    attachInterrupt(RAIN, rainIRQ, FALLING);
    // turn on interrupts
    interrupts();

}

void loop(){
  ArduinoOTA.handle();

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  //mqttAnemometer
  if(flagArray[0]){
    mqttAnemometer();
  }

  //mqttPluviometer
  if(flagArray[1]){
    mqttPluviometer();
  }
  
  //mqttVane
  if(flagArray[2]){
    mqttVane();
  }  
    
    //Keep track of which minute it is
    if (millis() - lastSecond >= 1000) {

        lastSecond += 1000;

        //Take a speed and direction reading every second for 2 minute average
        if (++seconds_2m > 119) {
            seconds_2m = 0;
        }

        //Calc the wind speed and direction every second for 120 second to get 2 minute average
        float currentSpeed = get_wind_speed();
        
        int currentDirection = get_wind_direction();
        windspdavg[seconds_2m] = (int)currentSpeed;
        winddiravg[seconds_2m] = currentDirection;
        
        //Check to see if this is a gust for the minute
        if (currentSpeed > windgust_10m[minutes_10m]) {
            windgust_10m[minutes_10m] = currentSpeed;
            windgustdirection_10m[minutes_10m] = currentDirection;
        }

        //Check to see if this is a gust for the day
        if (currentSpeed > windgustkmh) {
            windgustkmh = currentSpeed;
            windgustdir = currentDirection;
        }

        if (++seconds > 59) {
            seconds = 0;

            if (++minutes > 59) {
                minutes = 0;
            }
            if (++minutes_10m > 9) {
                minutes_10m = 0;
            }

            rainHour[minutes] = 0; //Zero out this minute's rainfall amount
            windgust_10m[minutes_10m] = 0; //Zero out this minute's gust
        }

        //Report all readings every second
        //printWeather();
    }

    delay(100);
}

/************************************ MQTT ************************************/
/**
 * Setup MQTT
 */
void setup_MQTT(){

    Serial.print("TESTE: ");
    Serial.print(mqtt_server);
    Serial.print(": ");
    Serial.println(String(mqtt_port).toInt());
    client.setServer(mqtt_server, String(mqtt_port).toInt());
    client.setCallback(callback);
}

/**
 * Callback MQTT
 */
void callback(char * top, byte * payload, unsigned int length){
    String topic = top;
    String message;

    char topicRead[100];

    for (int i = 0; i < length; i++){
        message += (char)
        payload[i];
    }
    Serial.println(topic);
    Serial.println(message);

    char * serialBuffer[6];
    int i = 0;
    //char *p = topic;
    char * str;
    while ((str = strtok_r(top, "/", & top)) !=NULL){
        Serial.println(str);
        serialBuffer[i++] = str;
    }
}

/**
 * Reconnect
 */
void reconnect(){
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect(idESP8266.c_str())) {
            Serial.println("connected");
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

/**
 * mqttPluviometer
 */
void mqttPluviometer(){
    long now = millis();
    if (now - lastUpdateTimeArray[0] > updateTimeArray[0]) {
        lastUpdateTimeArray[0] = now;
        snprintf(result, 200, "{\"rainin\": %d.%d, \"dailyrainin\": %d.%d}", (int) rainin, (int)((rainin - (int)rainin) *100), (int)dailyrainin, (int)((dailyrainin - (int)dailyrainin) *100));
        sprintf(channel_char, channelFormat.c_str(), idSensorPluviometer, 1, "read");
        client.publish(channel_char, result);
    }
}

/**
 * mqttAnemometer
 */
void mqttAnemometer(){
    long now = millis();
    if (now - lastUpdateTimeArray[1] > updateTimeArray[1]) {
        lastUpdateTimeArray[1] = now;
        snprintf(result, 200, "{\"windspeedkmh\": %d.%d, \"windgustkmh\": %d.%d, \"windspdkmh_avg2m\": %d.%d, \"windgustkmh_10m\": %d.%d}", (int) windspeedkmh, (int)((windspeedkmh - (int) windspeedkmh) * 100),(int)windgustkmh, (int)((windgustkmh - (int)windgustkmh) *100),(int)windspdkmh_avg2m, (int)((windspdkmh_avg2m - (int) windspdkmh_avg2m) *100),(int)windgustkmh_10m, (int)((windgustkmh_10m - (int)windgustkmh_10m) *100));

        sprintf(channel_char, channelFormat.c_str(), idSensorAnemometer, 1, "read");
        client.publish(channel_char, result);
    }
}

/**
 * mqttVane
 */
void mqttVane()
{
    long now = millis();
    if (now - lastUpdateTimeArray[2] > updateTimeArray[2]) {
        lastUpdateTimeArray[2] = now;
        snprintf(result, 200, "{\"winddir\": %d.%d, \"windgustdir\": %d.%d, \"winddir_avg2m\": %d.%d, \"windgustdir_10m\": %d.%d}", (int)winddir, (int)((winddir - (int)winddir) *100),(int) windgustdir, (int)((windgustdir - (int) windgustdir) *100),(int)winddir_avg2m, (int)((winddir_avg2m - (int)winddir_avg2m) *100), (int)windgustdir_10m, (int)((windgustdir_10m - (int)windgustdir_10m) *100));

        sprintf(channel_char, channelFormat.c_str(), idSensorVane, 1, "read");
        client.publish(channel_char, result);
    }
}


//Calculates each of the variables that wunderground is expecting
void calcWeather(){
    //Calc winddir
    winddir = get_wind_direction();

    //Calc windspeed
    windspeedkmh = get_wind_speed(); //This is calculated in the main loop

    //Calc windgustkmh
    //Calc windgustdir
    //These are calculated in the main loop

    //Calc windspdkmh_avg2m
    float temp = 0;
    for (int i = 0;i < 120;i++){
        temp += windspdavg[i];
    }
    temp /= 120.0;
    windspdkmh_avg2m = temp;

    //Calc winddir_avg2m, Wind Direction
    //You can't just take the average. Google "mean of circular quantities" for more info
    //We will use the Mitsuta method because it doesn't require trig functions
    //And because it sounds cool.
    //Based on: http://abelian.org/vlf/bearings.html
    //Based on: http://stackoverflow.com/questions/1813483/averaging-angles-again
    long sum = winddiravg[0];
    int D = winddiravg[0];
    for (int i = 1;i < WIND_DIR_AVG_SIZE;i++){
      int delta = winddiravg[i] - D;

        if (delta < -180)
            D += delta + 360;
        else if (delta > 180)
            D += delta - 360;
        else
            D += delta;

        sum += D;
    }
    winddir_avg2m = sum / WIND_DIR_AVG_SIZE;
    if (winddir_avg2m >= 360) {
        winddir_avg2m -= 360;
    }
    if (winddir_avg2m < 0) {
        winddir_avg2m += 360;
    }

    //Calc windgustkmh_10m
    //Calc windgustdir_10m
    //Find the largest windgust in the last 10 minutes
    windgustkmh_10m = 0;
    windgustdir_10m = 0;
    //Step through the 10 minutes
    for (int i = 0;i < 10;i++){
        if (windgust_10m[i] > windgustkmh_10m) {
            windgustkmh_10m = windgust_10m[i];
            windgustdir_10m = windgustdirection_10m[i];
        }
    }

    //Total rainfall for the day is calculated within the interrupt
    //Calculate amount of rainfall for the last 60 minutes
    rainin = 0;
    for (int i = 0;i < 60;i++){
        rainin += rainHour[i];
    }
}

//Returns the instataneous wind speed
float get_wind_speed(){
    float deltaTime = millis() - lastWindCheck; //750ms

    deltaTime /= 1000.0; //Covert to seconds

    float windSpeed = (float) windClicks / deltaTime; //3 / 0.750s = 4

    windClicks = 0; //Reset and start watching for new wind
    lastWindCheck = millis();

    windSpeed *= 1.492 * 2.4; //4 * 1.492 = 5.968MPH -> 9.60Km/h

     Serial.println();
     Serial.print("Windspeed:");
     Serial.println(windSpeed);

    return (windSpeed);
}

//Read the wind direction sensor, return heading in degrees
int get_wind_direction(){
    unsigned int adc;
    float aux2=0;
    
    adc = analogRead(WDIR); // get the current reading from the sensor

    // The following table is ADC readings for the wind direction sensor output, sorted from low to high.
    // Each threshold is the midpoint between adjacent headings. The output is degrees for that ADC reading.
    // Note that these are not in compass degree order! See Weather Meters datasheet for more information.

/* ERROR   
    if (adc < 380) {
        return (113);
    } else if (adc < 393) {
        return (68);
    } else if (adc < 414) {
        return (90);
    } else if (adc < 456) {
        return (158);
    } else if (adc < 508) {
        return (135);
    } else if (adc < 551) {
        return (203);
    } else if (adc < 615) {
        return (180);
    } else if (adc < 680) {
        return (23);
    } else if (adc < 746) {
        return (45);
    } else if (adc < 801) {
        return (248);
    } else if (adc < 833) {
        return (225);
    } else if (adc < 878) {
        return (338);
    } else if (adc < 913) {
        return (0);
    } else if (adc < 940) {
        return (293);
    } else if (adc < 967) {
        return (315);
    } else if (adc < 990) {
        return (270);
    } else {
        return (-1); // error, disconnected?
    }
    */
  aux2 = (adc*3.3)/1023; // Volts,  k
  if( aux2<0.25 ){
    Serial.println("SENS_AGR_VANE_ESE");
    return SENS_AGR_VANE_ESE;
  }
  else if( aux2>=0.25 && aux2<0.28 ){
    Serial.println("SENS_AGR_VANE_ENE");
    return SENS_AGR_VANE_ENE;
  }
  else if( aux2>=0.28 && aux2<0.35 ){
    Serial.println("SENS_AGR_VANE_E");
    return SENS_AGR_VANE_E;
  }
  else if( aux2>=0.35 && aux2<0.5 ){
    Serial.println("SENS_AGR_VANE_SSE");
    return SENS_AGR_VANE_SSE;
  }
  else if( aux2>=0.5 && aux2<0.65 ){
    Serial.println("SENS_AGR_VANE_SE");
    return SENS_AGR_VANE_SE;
  }
  else if( aux2>=0.65 && aux2<0.85 ){
    Serial.println("SENS_AGR_VANE_SSW");
    return SENS_AGR_VANE_SSW;
  }
  else if( aux2>=0.85 && aux2<1.1 ){
    Serial.println("SENS_AGR_VANE_S");
    return SENS_AGR_VANE_S;
  }
  else if( aux2>=1.1 && aux2<1.38 ){
    Serial.println("SENS_AGR_VANE_NNE");
    return SENS_AGR_VANE_NNE;
  }
  else if( aux2>=1.38 && aux2<1.6 ){
    Serial.println("SENS_AGR_VANE_NE");
    return SENS_AGR_VANE_NE;
  }
  else if( aux2>=1.6 && aux2<1.96 ){
    Serial.println("SENS_AGR_VANE_WSW");
    return SENS_AGR_VANE_WSW;
  }
  else if( aux2>=1.96 && aux2<2.15 ){
    Serial.println("SENS_AGR_VANE_SW");
    return SENS_AGR_VANE_SW;
  }
  else if( aux2>=2.15 && aux2<2.35 ){
    Serial.println("SENS_AGR_VANE_NNW");
    return SENS_AGR_VANE_NNW;
  }
  else if( aux2>=2.35 && aux2<2.6 ){
    Serial.println("SENS_AGR_VANE_N");
    return SENS_AGR_VANE_N;
  }
  else if( aux2>=2.6 && aux2<2.75 ){
    Serial.println("SENS_AGR_VANE_WNW");
    return SENS_AGR_VANE_WNW;
  }
  else if( aux2>=2.75 && aux2<2.95 ){
    Serial.println("SENS_AGR_VANE_NW");
    return SENS_AGR_VANE_NW;
  }
  else if( aux2>=2.95 ){
    Serial.println("SENS_AGR_VANE_W");
    return SENS_AGR_VANE_W;
  }
}


//Prints the various variables directly to the port
//I don't like the way this function is written but Arduino doesn't support floats under sprintf
void printWeather(){
    calcWeather(); //Go calc all the various sensors

    Serial.println();
    Serial.print("$,winddir=");
    Serial.print(winddir);
    Serial.print(",windspeedkmh=");
    Serial.print(windspeedkmh, 1);
    Serial.print(",windgustkmh=");
    Serial.print(windgustkmh, 1);
    Serial.print(",windgustdir=");
    Serial.print(windgustdir);
    Serial.print(",windspdkmh_avg2m=");
    Serial.print(windspdkmh_avg2m, 1);
    Serial.print(",winddir_avg2m=");
    Serial.print(winddir_avg2m);
    Serial.print(",windgustkmh_10m=");
    Serial.print(windgustkmh_10m, 1);
    Serial.print(",windgustdir_10m=");
    Serial.print(windgustdir_10m);
    Serial.print(",rainin=");
    Serial.print(rainin, 2);
    Serial.print(",dailyrainin=");
    Serial.print(dailyrainin, 2);
    Serial.print(",");
    Serial.println("#");
}
