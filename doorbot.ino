/*
 * This assumes an ESP32 connected to a MFRC522 RFID reader over SPI and an OLED screen connected over I2C
 */

#include <ESP.h>
#include <esp_int_wdt.h>
#include <esp_task_wdt.h>
#include <U8x8lib.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

const char* deviceName = "doorbot";
const char* ssid = "ssid";     // your network SSID (name of wifi network)
const char* password = "password"; // your network password
const char* host = "xxxxxxxxx.execute-api.eu-west-1.amazonaws.com";  // Membership API URL
const int wifiTimeoutSeconds = 20;

// the OLED used
U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(/* clock=*/ 15, /* data=*/ 4, /* reset=*/ 16);

#define SS_PIN 21
#define RST_PIN 22
MFRC522 mfrc522(SS_PIN, RST_PIN);



WiFiClientSecure client;
WebServer server(80);


void setup()
{
  Serial.begin(115200);

  setupRFID();
  setupScreen();
  connectWifi();
  setupMDNS();
  setupServer();
  resetScreen();
}

void loop()
{
  waitForRFID();
  server.handleClient();
  delay(1);
}




const char* loginIndex = 
 "<form name='loginForm'>"
    "<table width='20%' bgcolor='A09F9F' align='center'>"
        "<tr>"
            "<td colspan=2>"
                "<center><font size=4><b>ESP32 Login Page</b></font></center>"
                "<br>"
            "</td>"
            "<br>"
            "<br>"
        "</tr>"
        "<td>Username:</td>"
        "<td><input type='text' size=25 name='userid'><br></td>"
        "</tr>"
        "<br>"
        "<br>"
        "<tr>"
            "<td>Password:</td>"
            "<td><input type='Password' size=25 name='pwd'><br></td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
            "<td><input type='submit' onclick='check(this.form)' value='Login'></td>"
        "</tr>"
    "</table>"
"</form>"
"<script>"
    "function check(form)"
    "{"
    "if(form.userid.value=='admin' && form.pwd.value=='admin')"
    "{"
    "window.open('/serverIndex')"
    "}"
    "else"
    "{"
    " alert('Error Password or Username')/*displays error message*/"
    "}"
    "}"
"</script>";
 
/*
 * Server Index Page
 */
 
const char* serverIndex = 
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')" 
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>";


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
  if (!MDNS.begin(deviceName)) { //http://esp32.local
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
}

static void setupServer() {
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", loginIndex);
  });
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  /*handling uploading firmware file */
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  server.begin();
}


static void connectWifi() {
  static uint32_t ms = millis();
  static uint8_t watchdog = 0;
  
  Serial.print("Connecting Wifi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
    if((millis() - ms) > 1000 * wifiTimeoutSeconds) {
      hard_restart();
    }
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);
}

static void resetScreen() {
  u8x8.clearDisplay();
  u8x8.drawString(0, 0, "Welcome to");
  u8x8.drawString(0, 1, "Teesside");
  u8x8.drawString(0, 2, "Hackspace!");
  u8x8.drawString(0, 3, "Please scan");
  u8x8.drawString(0, 4, "your keyfob");
}

static String callApi(String uid) {
  String title = "";
  String headers = "";
  String body = "";
  bool finishedHeaders = false;
  bool currentLineIsBlank = true;
  bool gotResponse = false;
  if (client.connect(host, 443)) {
    Serial.println("connected");

    String URL = "/prod/rfid/" + uid + "/" + deviceName;

    Serial.println(URL);

    client.println("GET " + URL + " HTTP/1.1");
    client.print("Host: ");
    client.println(host);
    client.println("User-Agent: arduino/1.0");
    client.println("");

    while (!client.available()) {
      delay(100);
    }
    
    while (client.available()) {
      char c = client.read();

      if (finishedHeaders) {
        body = body + c;
      } else {
        if (currentLineIsBlank && c == '\n') {
          finishedHeaders = true;
        } else {
          headers = headers + c;
        }
      }

      if (c == '\n') {
        currentLineIsBlank = true;
      } else if (c != '\r') {
        currentLineIsBlank = false;
      }
      gotResponse = true;
    }
    if (gotResponse) {
      return body;
    }
  }
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
  mfrc522.PICC_DumpDetailsToSerial(&(mfrc522.uid)); 
  //Show UID on serial monitor
  Serial.print("UID tag :");
  String content= "";
  byte letter;
  for (byte i = 0; i < mfrc522.uid.size; i++) 
  {
     Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
     Serial.print(mfrc522.uid.uidByte[i], HEX);
     content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
     content.concat(String(mfrc522.uid.uidByte[i], HEX));
  }
  Serial.println();
  Serial.print("Message : ");
  content.toUpperCase();

  //char uid[64];
  //content.toCharArray(uid, 64);
  u8x8.clearDisplay();
  u8x8.drawString(0, 0, "Please wait...");

  String body = callApi(content);
  Serial.println(body);
  StaticJsonBuffer<1000> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(body);
  
  u8x8.clearDisplay();
  // Test if parsing succeeds.
  if (!root.success()) {
    Serial.println("parseObject() failed");
    u8x8.drawString(0, 0, "An error ocurred");
    u8x8.drawString(0, 1, "Please try again");
    u8x8.drawString(0, 2, "If this persists");
    u8x8.drawString(0, 3, "please contact");
    u8x8.drawString(0, 4, "the trustees");
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
    
      u8x8.drawString(0, 0, "Hello");
      u8x8.drawString(0, 1, name);
    }
  }
  
  delay(5000);
  ESP.restart();
}

void hard_restart() {
  esp_task_wdt_init(1,true);
  esp_task_wdt_add(NULL);
  while(true);
}
