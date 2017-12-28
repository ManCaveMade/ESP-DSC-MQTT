
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <DSC.h>

// Required for LIGHT_SLEEP_T delay mode
extern "C" {
#include "user_interface.h"
}

const char *host = "espDSC";
const char *ssid = "Ubernet";
const char *password = "BDD5A42641";
const char *ntpServer = "za.pool.ntp.org";

#define MQTT_HOST IPAddress(192, 168, 0, 6)
#define MQTT_PORT 1883
#define MQTT_USER "home"
#define MQTT_PASS "HAss7412369"

const char *MQTT_TOPIC = "espdsc/verbose";
const char *MQTT_ZONE_TOPIC = "espdsc/zone";
const char *MQTT_STATUS_TOPIC = "espdsc/status";

#define SLEEP_MS 10 //100ms

#define CLK_PIN 4
#define DATA_PIN 5
#define DATA_PIN_OUT 13
#define LED_PIN 12

bool wifiConnected = false;
bool mqttConnected = false;

//NTP Stuff
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, 7200);

//Web Server Stuff
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

String webString = "";

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

DSC dsc;

void connectToWifi()
{
  //Serial.println("Connecting to Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  //WiFi.setOutputPower(TX_POWER);
  wifi_set_sleep_type(LIGHT_SLEEP_T);
}

void onWifiConnect(const WiFiEventStationModeGotIP &event)
{
  //Serial.println("Connected to Wi-Fi.");
  wifiConnected = true;
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected &event)
{
  //Serial.println("Disconnected from Wi-Fi.");
  wifiConnected = false;
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void connectToMqtt()
{
  //Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent)
{
  mqttConnected = true;
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  //Serial.println("Disconnected from MQTT.");
  mqttConnected = false;
  if (WiFi.isConnected())
  {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {}

void onMqttUnsubscribe(uint16_t packetId) {}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
  //  Serial.println("Publish received.");
  //  Serial.print("  topic: ");
  //  Serial.println(topic);
  //  Serial.print("  qos: ");
  //  Serial.println(properties.qos);
  //  Serial.print("  dup: ");
  //  Serial.println(properties.dup);
  //  Serial.print("  retain: ");
  //  Serial.println(properties.retain);
  //  Serial.print("  len: ");
  //  Serial.println(len);
  //  Serial.print("  index: ");
  //  Serial.println(index);
  //  Serial.print("  total: ");
  //  Serial.println(total);
}

void onMqttPublish(uint16_t packetId) {}

void handleRoot()
{
  server.send(200, "text/html", "<html> <head> <meta http-equiv='refresh' content='1'/> <title>ESP-DSC-MQTT</title> <style>body{background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088;}</style> </head> <body> <p>" + webString + "</p></body></html>");
}

void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setup(void)
{
  Serial.begin(115200);
  delay(100);

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  connectToWifi();

  MDNS.begin(host);

  //Connected. Do the rest of the setup
  timeClient.begin();

  server.on("/", handleRoot);
  server.on("/json", []() {
    server.send(200, "application/json", webString);
  });

  server.onNotFound(handleNotFound);

  httpUpdater.setup(&server);

  server.begin();

  MDNS.addService("http", "tcp", 80);

  dsc.setCLK(CLK_PIN);
  dsc.setDTA_IN(DATA_PIN);
  dsc.setDTA_OUT(DATA_PIN_OUT);
  dsc.setLED(LED_PIN);

  dsc.begin();

  //Ready to work!
  Serial.println("https://github.com/ManCaveMade/ESP-DSC-MQTT");
  Serial.printf("Open http://%s.local/update in your browser to update firmware.\n", host);
  Serial.println("MQTT Topic: " + String(MQTT_TOPIC));

  webString = "Nothing received yet...\nTime:" + timeClient.getFormattedTime();
  
}

void loop(void)
{
  if (wifiConnected)
  {
    timeClient.update();
    server.handleClient();
  }

  if (dsc.process())
  {
    if (dscGlobal.pCmd)
    {

      Serial.println(dscGlobal.pMsg);

      //stuff from serial is json without brackets so we can add more
      webString = "{\"Time\":\"" + timeClient.getFormattedTime() +
                  "\",\"EpochSeconds\":" + timeClient.getEpochTime() +
                  ",\"PanelRaw\":\"" + String(dsc.pnlRaw()) +
                  "\",\"PanelCommandHex\":\"" + String(dscGlobal.pCmd, HEX) +
                  "\",\"PanelMessage\":" + String(dscGlobal.pMsg) +
                  "}";

      if (mqttConnected)
      {
        //verbose (everything)
        mqttClient.publish(MQTT_TOPIC, 0, false, webString.c_str());

        if ((dscGlobal.pCmd == 0x05) || (dscGlobal.pCmd == 0xA5)) { //Status
          mqttClient.publish(MQTT_STATUS_TOPIC, 1, false, webString.c_str());
        } else if ((dscGlobal.pCmd == 0x27) || (dscGlobal.pCmd == 0x2D)) { //Zones
          mqttClient.publish(MQTT_ZONE_TOPIC, 1, false, webString.c_str());
        }
      }
    }
  }

  //Sleep for a while
 // long endMs = millis() + SLEEP_MS;
//  while (millis() < endMs)
//  {
 //   yield();
 // }
}
