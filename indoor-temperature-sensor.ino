/*
  MQTT Temperature Client
  Hardware Setup:
  DS18B20 Connected on Pin 2 / D4

  OTA Uploads - default port of 8266
  TEMP_SERVER - server IP
  MQTT_PORT   - default of 1883

  Requires definition of WIFI_HOME or WIFI_GARAGE for proper inclusion of SSID/PSK
  
  Reads temperature every 5m and posts it to the "data" topic on the MQTT Server
    - message has this format KEY:VALUE,KEY:VALUE,KEY:VALUE - e.g. "HOSTNAME:ESP_SDF897,TEMP:33.97,BATTERY:82.98"
    - any key names can be used, key names cannot be repeated
    - HOSTNAME is used to identify each sensor uniquely
    - HOSTNAME is based off of the MAC addr of each sensor
    - If a battery is installed; uncomment line 23 (BATTERY_INSTALLED)
  Subscribes to the TEMP_REQ topic
    - when a 1 is received
    - it sends an immediate update from the temperature sensor
*/
#define WIFI_GARAGE
// #define BATTERY_INSTALLED
#define DEBUG
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <WifiCreds.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "message_data.h"
#define ONE_WIRE_BUS D3
#define MQTT_PORT 1883
uint8_t MAC_array[6];
char MAC_char[18];
#define MAX_AWAKE_MS 15 * 1000 // 15,000ms 15s
#define TEMP_MESSAGE_INTERVAL_MS 300 * 1000 // 300s 5m
#define TEMP_READ_INTERVAL_MS 5 * 1000 // 5s
#define DEBUG_COUNTDOWN_INTERVAL_MS 10 * 1000 // 10s
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
// define our wifi client and the MQTT client
WiFiClient        espClient;
PubSubClient      client(espClient);

unsigned long lastTempMessageSentAt = 0;
unsigned long lastTempReadAt        = 0;
bool firstBoot;
char msg[50];
char currentHostname[14];


float getTemp(){
  float temp;
  sensors.requestTemperatures();
  temp = sensors.getTempFByIndex(0);
  return temp;
}

void setup() {
  firstBoot = true;
  Serial.begin(115200);
  setup_wifi();
  client.setServer(TEMP_SERVER, MQTT_PORT);
  client.setCallback(callback);
  // Setup OTA Uploads
  ArduinoOTA.setPassword((const char *)"boarding");

  ArduinoOTA.onStart([]() {
    Serial.println("STARTING OTA UPDATE");
    char updateMessage[36]= "OTA UPDATE START - ";
    strcat(updateMessage, currentHostname);
    client.publish("debugMessages", updateMessage);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nCOMPLETED OTA UPDATE");
    char updateMessage[37]= "OTA UPDATE FINISH - ";
    strcat(updateMessage, currentHostname);
    client.publish("debugMessages", updateMessage);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

}

void setup_wifi() {

  delay(10);

  Serial.println();
  Serial.print("Connecting to ");
  WiFi.mode(WIFI_STA);
  Serial.println(MY_SSID);
  WiFi.begin(MY_SSID, MY_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  // Store the hostname to use later for MQTT ID
  String hostnameString = WiFi.hostname();
  hostnameString.toCharArray(currentHostname, 14);
  Serial.print("Hostname: ");
  Serial.println(WiFi.hostname());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  payload[length] = '\0';
  char *payloadS = (char *) payload;

  // if (strcmp(topic, "TEMP_REQ") == 0){
  //   if ((char)payload[0] == '1') {
  //     Serial.println("Temperature update requested!");
  //     float temp = getTemp();
  //     Serial.println("current functionality is not implemented")
  //   }
  // }
}


void reconnect() 
{
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(currentHostname)) {
      Serial.println("connected");
      // ... and resubscribe
      client.subscribe("SET_INTERVAL");
      client.subscribe("TEMP_REQ");
    } else {
      Serial.print("failed, reasoncode=");
      Serial.print(client.state());
      Serial.println(" trying again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

bool sendMessage(char* topic, char* message)
{
  reconnect(); // reconnect() is a no-op if we're connected
  // multiple calls to client.loop() to ensure the client has enough time
  // to loop and execute correctly
  // can be inconsistent sometimes without them
  client.loop();
  #ifdef DEBUG
  Serial.println("Sending Message");
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  Serial.println(message);
  #endif
  
  bool result = client.publish(topic, message);
  
  #ifdef DEBUG
  Serial.print("Result: ");
  Serial.println(result);
  #endif
  
  client.loop();
  return result;
}

bool sendMessage_v2(MessageData data)
{
  char message[48];
  sprintf(message, "HOSTNAME:%s,TEMP:%s", data.hostname, data.temperature);
  bool result = sendMessage("data", message);
  return result;
}

bool sendUpdate(MessageData data, float temp, float batt)
{
  bool result;
  
  dtostrf(batt,4,2,data.battery);
  dtostrf(temp,4,2,data.temperature);
  
  result = sendMessage_v2(data);
  // continue with v1
  if(result){
    lastTempMessageSentAt = millis();
  }
  delay(1500);
  return result;
}

bool isTimeForUpdate()
{
  long millisOffsetValue      = lastTempMessageSentAt + TEMP_MESSAGE_INTERVAL_MS;
  long timeRemainingForUpdate = millisOffsetValue - millis();
  
  bool result = millis() > millisOffsetValue;

    // #ifdef DEBUG
    // Serial.print("Time remaining until next update: ");
    // Serial.print(timeRemainingForUpdate / 1000);
    // Serial.println("s");
    // #endif
  
  
  return result;
}

float calculateAverageOfArray(float num[100], int number_of_elements)
{
    int i;
    float sum = 0.0, average;
    
    for(i = 0; i < number_of_elements; ++i)
    {
        sum += num[i];
    }
    average = sum / number_of_elements;
    return average;
}

bool invalidTempReading(float temp)
{
  bool result = (temp < -20.0 || temp > 120.0 || temp == 0.000 );
  return result;
}

void loop()
{
  ArduinoOTA.handle();
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  float temp = 0.000;
  float batt = 0.000;
  MessageData data;
  data.hostname = currentHostname;

  while(invalidTempReading(temp) && millis() > (lastTempReadAt + TEMP_READ_INTERVAL_MS) ){
    // while loop so we read the temperature from the sensor constantly
    // if there is not a good reading
    temp           = getTemp();
    lastTempReadAt = millis();
    
    #ifdef BATTERY_INSTALLED
    batt = fuelGauge.stateOfCharge();
    #endif
    
    #ifdef DEBUG
    Serial.print("Temp: ");
    Serial.println(temp);
    Serial.print("Batt: ");
    Serial.println(batt);
    #endif
  }
  // if it's time to send an update and we've got a valid temperature
  // send the update
  
  if((firstBoot || isTimeForUpdate()) && !invalidTempReading(temp) ){
    sendUpdate(data, temp, batt);
    firstBoot = false;
  }
}
