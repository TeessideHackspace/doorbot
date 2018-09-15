/*
 * This assumes a NodeMCU connected to a MFRC522 RFID reader over SPI and an OLED screen connected over I2C
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <MQTT.h>
#include <IotWebConf.h>

#include <ESP.h>
#include <U8x8lib.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include <ESP8266mDNS.h> 
#include <string.h>
#include <Hash.h>

// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const String deviceName = "door";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "********";

#define STRING_LEN 128

// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "mqt1"

// -- When CONFIG_PIN is pulled to ground on startup, the Thing will use the initial
//      password to buld an AP. (E.g. in case of lost password)
#define CONFIG_PIN 2

// -- Status indicator pin.
//      First it will light up (kept LOW), on Wifi connection it will blink,
//      when connected to the Wifi it will turn off (kept HIGH).
#define STATUS_PIN LED_BUILTIN

// -- Callback method declarations.
void wifiConnected();
void configSaved();
boolean formValidator();
void mqttMessageReceived(String &topic, String &payload);

DNSServer dnsServer;
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
WiFiClient net;
MQTTClient mqttClient;

char mqttServerValue[STRING_LEN];
char mqttUserNameValue[STRING_LEN];
char mqttUserPasswordValue[STRING_LEN];

IotWebConf iotWebConf(deviceName.c_str(), &dnsServer, &httpServer, wifiInitialApPassword, CONFIG_VERSION);
IotWebConfParameter mqttServerParam = IotWebConfParameter("MQTT server", "mqttServer", mqttServerValue, STRING_LEN);
IotWebConfParameter mqttUserNameParam = IotWebConfParameter("MQTT user", "mqttUser", mqttUserNameValue, STRING_LEN);
IotWebConfParameter mqttUserPasswordParam = IotWebConfParameter("MQTT password", "mqttPass", mqttUserPasswordValue, STRING_LEN, "password");

boolean needMqttConnect = false;
boolean needReset = false;
int pinState = HIGH;
unsigned long lastReport = 0;
unsigned long lastMqttConnectionAttempt = 0;

const String requestTopic = "teessideHackspace/access/" +  deviceName + "/request";
const String responseTopic = "teessideHackspace/access/" + deviceName + "/response";

const int mqttTimeoutSeconds = 10;

// the OLED used
U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(/* clock=*/ 10, /* data=*/ 0, /* reset=*/ U8X8_PIN_NONE);

#define SS_PIN 4
#define RST_PIN 5
MFRC522 mfrc522(SS_PIN, RST_PIN);

void setup()
{
  Serial.begin(9600);
  setupWebConf();
  setupRFID();
  setupScreen();
  setupMDNS();
  resetScreen();
}

void loop()
{
  iotWebConf.doLoop();
  reconnectMqtt();

  handleReset();
  
  waitForRFID();
  delay(1);
}

static void setupWebConf() {
  iotWebConf.setStatusPin(STATUS_PIN);
  iotWebConf.setConfigPin(CONFIG_PIN);
  iotWebConf.addParameter(&mqttServerParam);
  iotWebConf.addParameter(&mqttUserNameParam);
  iotWebConf.addParameter(&mqttUserPasswordParam);
  iotWebConf.setConfigSavedCallback(&configSaved);
  iotWebConf.setFormValidator(&formValidator);
  iotWebConf.setWifiConnectionCallback(&wifiConnected);
  iotWebConf.setupUpdateServer(&httpUpdater);

  // -- Initializing the configuration.
  boolean validConfig = iotWebConf.init();
  if (!validConfig)
  {
    mqttServerValue[0] != '\0';
    mqttUserNameValue[0] != '\0';
    mqttUserPasswordValue[0] != '\0';
  }

  // -- Set up required URL handlers on the web server.
  httpServer.on("/", handleRoot);
  httpServer.on("/config", []{ iotWebConf.handleConfig(); });
  httpServer.onNotFound([](){ iotWebConf.handleNotFound(); });

  mqttClient.begin(mqttServerValue, net);
  mqttClient.onMessage(mqttMessageReceived);
  
  Serial.println("Ready.");
}

void wifiConnected()
{
  needMqttConnect = true;
}

void configSaved()
{
  Serial.println("Configuration was updated.");
  needReset = true;
}

boolean formValidator()
{
  Serial.println("Validating form.");
  boolean valid = true;

  int l = httpServer.arg(mqttServerParam.id).length();
  if (l < 3) {
    mqttServerParam.errorMessage = "Please provide at least 3 characters!";
    valid = false;
  }

  return valid;
}

void reconnectMqtt() {
  if (needMqttConnect) {
    if (connectMqtt()) {
      needMqttConnect = false;
    }
  }
}

boolean connectMqtt() {
  unsigned long now = millis();
  if ((lastMqttConnectionAttempt + 1000) > now) {
    // Do not repeat within 1 sec.
    return false;
  }
  Serial.println("Connecting to MQTT server...");
  if (!connectMqttOptions()) {
    lastMqttConnectionAttempt = now;
    return false;
  }
  Serial.println("Connected!");

  mqttClient.subscribe(responseTopic);
  return true;
}

boolean connectMqttOptions()
{
  boolean result;
  if (mqttUserPasswordValue[0] != '\0') {
    result = mqttClient.connect(iotWebConf.getThingName(), mqttUserNameValue, mqttUserPasswordValue);
  }
  else if (mqttUserNameValue[0] != '\0') {
    result = mqttClient.connect(iotWebConf.getThingName(), mqttUserNameValue);
  }
  else {
    result = mqttClient.connect(iotWebConf.getThingName());
  }
  return result;
}

static void handleReset() {
  if (needReset) {
    Serial.println("Rebooting after 1 second.");
    iotWebConf.delay(1000);
    ESP.restart();
  }
}

static void setupRFID() {
  SPI.begin();      // Initiate  SPI bus
  mfrc522.PCD_Init();   // Initiate MFRC522
}

static void setupScreen() {
  u8x8.begin();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
}

static void setupMDNS() {
  /*use mdns for host name resolution*/
  if (!MDNS.begin(deviceName.c_str())) { //http://esp32.local
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
}

/**
 * Handle web requests to "/" path.
 */
void handleRoot()
{
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>IotWebConf 06 MQTT App</title></head><body>MQTT App demo";
  s += "<ul>";
  s += "<li>MQTT server: ";
  s += mqttServerValue;
  s += "</ul>";
  s += "Go to <a href='config'>configure page</a> to change values.";
  s += "</body></html>\n";

  httpServer.send(200, "text/html", s);
}


static void resetScreen() {
  u8x8.clearDisplay();
  u8x8.drawString(0, 0, "Welcome to");
  u8x8.drawString(0, 1, "Teesside");
  u8x8.drawString(0, 2, "Hackspace!");
  u8x8.drawString(0, 3, "Please scan");
  u8x8.drawString(0, 4, "your keyfob");
}

void mqttMessageReceived(String &topic, String &payload) {
 Serial.print("Message arrived [");
 Serial.print(topic);
 Serial.print("] ");
 //String body = callApi(content);
  //Serial.println(payload);
  StaticJsonBuffer<1000> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(payload);
  
  u8x8.clearDisplay();
  // Test if parsing succeeds.
  if (!root.success()) {
    Serial.println("parseObject() failed");
    error();
  } else {
    const String error = root["message"];
    if(error != "") {
      Serial.println(error);
      u8x8.drawString(0, 0, "Member not found");
      u8x8.drawString(0, 1, "If you think");
      u8x8.drawString(0, 2, "this is an error");
      u8x8.drawString(0, 3, "please contact");
      u8x8.drawString(0, 4, "the trustees");
    } else {
      
      const String firstName = root["first_name"];
      const String lastName = root["last_name"];
      const String fullName = firstName + " " + lastName;
      char name[64];
      fullName.toCharArray(name, 64);

      Serial.println("Found user!");
      Serial.println(name);
    
      u8x8.drawString(0, 0, "Hello");
      u8x8.drawString(0, 1, name);
    }
  }

  delay(5000);
  ESP.restart();
}

static void waitForRFID()
{
  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  // Select one of the cards
  if ( ! mfrc522.PICC_ReadCardSerial()) {
    return;
  }
  Serial.println("Found RFID");
  mfrc522.PICC_DumpDetailsToSerial(&(mfrc522.uid)); 
  //Show UID on serial monitor
  Serial.print("UID tag :");
  String content= "";
  byte letter;
  for (byte i = 0; i < mfrc522.uid.size; i++) 
  {
     Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
     Serial.print(mfrc522.uid.uidByte[i], HEX);
     content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : ""));
     content.concat(String(mfrc522.uid.uidByte[i], HEX));
  }
  Serial.println();
  Serial.print("Message : ");
  content.toUpperCase();

  //char uid[64];
  //content.toCharArray(uid, 64);
  u8x8.clearDisplay();
  u8x8.drawString(0, 0, "Please wait...");

  const String sha = sha1(content);
  Serial.println(sha);
  
  mqttClient.publish(requestTopic.c_str(), sha.c_str());

  static uint32_t ms = millis();
  while ((millis() - ms) < 1000 * mqttTimeoutSeconds) {
    Serial.println("Waiting for MQTT Response");
    mqttClient.loop();
  }

  Serial.println("Did not receive MQTT Response");
  error();
}

void error() {
    u8x8.clearDisplay();
    u8x8.drawString(0, 0, "An error ocurred");
    u8x8.drawString(0, 1, "Please try again");
    u8x8.drawString(0, 2, "If this persists");
    u8x8.drawString(0, 3, "please contact");
    u8x8.drawString(0, 4, "the trustees");
    delay(5000);
    ESP.restart();
}
