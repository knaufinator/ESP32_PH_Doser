#include <WiFi.h>
#include <Wire.h>
#include <AsyncMqttClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <sstream>

extern "C" {
  #include "freertos/FreeRTOS.h"
  #include "freertos/timers.h"
}

using namespace std;

#define down 19
#define up 18                
#define address 99

uint64_t chipid; 
               
#define WIFI_SSID "ssid"
#define WIFI_PASSWORD "pass"

#define userName  "mqttuser"
#define password  "mqttpassword"

//ph
char computerdata[20];           
byte received_from_computer = 0; 
byte code = 0;                   
char ph_data[20];                
byte i = 0;                      
int time_ = 900;                 
float ph_float;                  

TimerHandle_t pHCheckTimer;
TimerHandle_t pHDataCheckTimer;
TimerHandle_t pHDoserTimer;


//mqtt
AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

#define MQTT_HOST IPAddress('192.168.1.1')//your mqtt broker
#define MQTT_PORT 8100

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");

   std::ostringstream o;
   o << chipid;
   string id = o.str();
   mqttClient.setClientId(id.c_str());
   mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event) {
  Serial.printf("[WiFi-event] event: %d\n", event);
  switch(event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
      connectToMqtt();
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      Serial.println("WiFi lost connection");
      xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
      xTimerStart(wifiReconnectTimer, 0);
      break;
  }
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);

  //deviceid/PH/cal - 4,7,10
  std::ostringstream o;
  o << chipid << "/PH/dose"; //-1,1
  string doseTopic = o.str();

  std::ostringstream cal;
  cal << chipid << "/PH/cal"; //4,7,10
  string calTopic = cal.str();
  
  uint16_t packetIdSub = mqttClient.subscribe(doseTopic.c_str(), 0);
  uint16_t packetIdSub2 = mqttClient.subscribe(calTopic.c_str(), 0);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  String messageTemp;
  for (int i = 0; i < len; i++) {
    messageTemp += (char)payload[i];
  }


//deviceid/PH/dose

Serial.print(topic);
Serial.println(payload);

   std::ostringstream o;
   o << chipid << "/PH/dose";
   string doseTopic = o.str();

  //dose
  if (strcmp(topic, doseTopic.c_str()) == 0) {
    if (messageTemp == "1") {
     adjustPHStart(1);
    } 
    else if (messageTemp == "-1") {
     adjustPHStart(-1);
    }
  }

  std::ostringstream oo;
   oo << chipid << "/PH/cal";
  string calTopic = oo.str();
   
  //calibrate
  if (strcmp(topic, calTopic.c_str()) == 0) {
    if (messageTemp == "4") {
      Cal4();
    } 
    else if (messageTemp == "7") {
      Cal7();
    }
    else if (messageTemp == "10") {
      Cal10();
    }
  }
}


void Cal4()
{
    Wire.beginTransmission(address);     
    Wire.write("cal,low,4");       
    Wire.endTransmission();        
}

void Cal10()
{
    Wire.beginTransmission(address); 
    Wire.write("cal,high,10");       
    Wire.endTransmission();        
}

void Cal7()
{
    Wire.beginTransmission(address); 
    Wire.write("cal,mid,7");       
    Wire.endTransmission();   
}

bool isDoseDownRunning = false;
bool isDoseUpRunning = false;

void adjustPHStart(int dir)
{
  //dont start if either are already doign stuff
  if(isDoseDownRunning || isDoseUpRunning)
    return;

  if(dir == 1)  
  {
    digitalWrite(up,true);
    isDoseUpRunning = true;
  } 
  else if(dir == -1)
  {
    digitalWrite(down,true);
    isDoseDownRunning = true;
  }
  
   xTimerStart(pHDoserTimer, 0);   
}

void setup() {
  
  Serial.begin(115200);
  chipid=ESP.getEfuseMac();//The chip ID is essentially its MAC address(length: 6 bytes).

  pinMode (up, OUTPUT);
  pinMode (down, OUTPUT);
   
  Wire.begin();

  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));
  
  pHDoserTimer = xTimerCreate("pHDoserTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(pHDoserEnd));
  pHCheckTimer = xTimerCreate("pHCheckTimer", pdMS_TO_TICKS(100), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(pHCheck));
  pHDataCheckTimer = xTimerCreate("pHDataCheckTimer", pdMS_TO_TICKS(900), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(pHDataCheck));


  WiFi.onEvent(WiFiEvent);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCredentials(userName,password);
  
  connectToWifi();
  xTimerStart(pHCheckTimer, 0);
}

void pHDoserEnd()
{
  if(isDoseUpRunning)  
  {
      digitalWrite(up,false);
      isDoseUpRunning = false;
  }
  else if(isDoseDownRunning)
  {
      digitalWrite(down,false);
      isDoseDownRunning = false;
   } 
}

void loop() {
  
}

void requestPHDataRefresh()
{
    Wire.beginTransmission(address); 
    Wire.write('r');       
    Wire.endTransmission();        
}

void serviceI2C()
{
    byte in_char = 0; 
    
    Wire.requestFrom(address, 20, 1); //call the circuit and request 20 bytes (this may be more than we need)
    code = Wire.read();               //the first byte is the response code, we read this separately.
    
    float oldPH = ph_float;

    if(code == 1)//1 is sucess, else,...? ... reconnect/?
    {
        while (Wire.available()) {         //are there bytes to receive.
          in_char = Wire.read();           //receive a byte.
          ph_data[i] = in_char;            //load this byte into our array.
          i += 1;                          //incur the counter for the array element.
          if (in_char == 0) {              //if we see that we have been sent a null command.
            i = 0;                         //reset the counter i to 0.
            Wire.endTransmission();        //end the I2C data transmission.
            break;                         //exit the while loop.
          }
        }

        Serial.println(ph_data);  
        ph_float=atof(ph_data);
    }
}


void pHCheck()
{
    Wire.beginTransmission(address); 
    Wire.write('r');       
    Wire.endTransmission();    
    
    xTimerStart(pHDataCheckTimer, 0);
}

void pHDataCheck()
{
    serviceI2C();     

   std::ostringstream o;
   o << chipid << "/PH/value";
   string valueTopic = o.str();
    
    Serial.print("Publishing on PH/value: " );
    Serial.print(valueTopic.c_str());
    Serial.println(String(ph_float, 3).c_str());  
 
    if(mqttClient.connected())
    {
      uint16_t packetIdPub2 = mqttClient.publish(valueTopic.c_str(), 2, true,String(ph_float, 3).c_str() );      
    }
    
    xTimerStart(pHCheckTimer, 0);
}
