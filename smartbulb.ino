/*
  Motion-Sensing Warning Lights
  This sketch will wait for a high input on D2 from the PIR motion
  sensor, and then flash WiFi-connected lights to alert the room.
  Circuit:
   HC-SR501 PIR motion sensor attached, input on digital pin 2
  created 30 July 2020
  by Matt Taylor
*/

#include <SPI.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "config.h"

int status = WL_IDLE_STATUS;

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
 
const int KEY_INDEX = 0;            // your network key Index number (needed only for WEP)

const int MOTION_PIN = 2;              // motion is read on digital pin 2

const unsigned int LOCAL_PORT = 2444;       // the UDP port to use for the arduino
const unsigned int lightPort = 38899;      // the UDP port the lights are listening on; change this to fit your lights

// The static IPs of the lights
// Change these to match the (preferably static) IPs of the WiZ-enabled lights you're using.
// You can use any arbitrary number of lights and the program will work the same.
IPAddress lights[256]; // max possible lights
int numLights = 0;   

const char GET_BUFFER[] = "{\"method\":\"getPilot\"}"; // packet to get the state of a light
const char ON_BUFFER[] = "{\"method\":\"setPilot\",\"params\":{\"state\": 1}}";       // packet to turn off a light
const char OFF_BUFFER[] = "{\"method\":\"setPilot\",\"params\":{\"state\": 0}}";       // packet to turn off a light
char packetBuffer[255];



struct WizDeviceInfo {
  String mac;
  String moduleName;
  String fwVersion;
  String friendlyName;
  String ip; // Optional: store IP too
  bool state;
  int r;
  int g;
  int b;
  int brightness;
  int speed;
  int temp;
  String scene;
  int effectId;
};
WizDeviceInfo bulbs[16];
int bulbCount = 0;


WiFiUDP Udp;

void setup() {
  // Set the motion sensor's pin to input mode

  //Initialize serial
  Serial.begin(115200);

  Serial.print("Targeting the following lights on port: ");
  Serial.println(lightPort);

 
 
  // attempt to connect to Wifi network:
  WiFi.begin(wifi_ssid, wifi_password);
  Serial.print("Attempting to connect to SSID: ");

  Serial.println(wifi_ssid);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    // wait 5 seconds for connection:
    delay(100);
  }

  Serial.println("Connected to WiFi");
  printWifiStatus();

  Serial.println("Waiting for motion...");
  Udp.begin(LOCAL_PORT);
  populateLights();
  for (int i = 0; i < numLights; i++) {
    IPAddress lightIP = lights[i];
    requestWizState(lightIP, lightPort);
    WizDeviceInfo bulb;
    
    if (receiveWizState(bulb)) {
      Serial.printf("State: %s\n", bulb.state ? "ON" : "OFF");
    } else {
      bulbs[bulbCount];
      bulbCount++;
    }
    requestWizSystemConfig(lightIP, lightPort);
    if (receiveWizSystemConfig(bulb)) {
      Serial.printf("MAC: %s\n", bulb.mac.c_str());
      Serial.printf("Name: %s\n", bulb.friendlyName.c_str());
    }
  }
}

void packetFlush() {
  // parse all packets in the RX buffer to clear it
  while (Udp.parsePacket()) {
    ;
  }
}

bool areLightsOn() {
  bool lightsOn = true;

  for (int i = 0; i < numLights; i++) {
    IPAddress lightIP = lights[i];
    Serial.println(lightIP);
    if (!isLightOn(lightIP, lightPort)) {
      // toggle this value to false if any one light in the array is turned off
      lightsOn = false;
    }
  }
  Serial.print("Are lights on? ");
  Serial.println(lightsOn ? "Yes" : "No");
  
  // if this is still true, it means every light was on
  return lightsOn;
}

bool isLightPresent(const IPAddress& lightIP, const uint16_t& lightPort) {
  bool lightOn = false;
  // flush RX buffer to avoid reading old packets
  packetFlush();
  Udp.beginPacket(lightIP, lightPort);
  Udp.write(GET_BUFFER);
  Udp.endPacket();
  int packetSize =  0;
  int timeout = 120;
  while (!packetSize && timeout > 0) {
    // wait until a reply is sent
    packetSize = Udp.parsePacket();
    timeout--;
    delay(1);
  }
  if (timeout == 0) {
    return false;
  } else {
    return true;
  }
  
}


bool isLightOn(const IPAddress& lightIP, const uint16_t& lightPort) {
  bool lightOn = false;

  // flush RX buffer to avoid reading old packets
  packetFlush();
  Udp.beginPacket(lightIP, lightPort);
  Udp.write(GET_BUFFER);
  Udp.endPacket();
  
  int packetSize = Udp.parsePacket();
  int timeout = 100;
  while (!packetSize && timeout > 0) {
    // wait until a reply is sent
    packetSize = Udp.parsePacket();
    timeout--;
    delay(200);
  }
  if (timeout == 0) {
    Serial.print("Error: light timed out: ");
    Serial.print(lightIP);
    Serial.print(" on port ");
    Serial.println(lightPort);
    return false;
  }
  if (packetSize) {
    // read the packet into packetBufffer
    int len = Udp.read(packetBuffer, 255);
    if (len > 0) {
      packetBuffer[len] = 0;
    }
    
    Serial.println("Packet received: ");
    Serial.println(packetBuffer);

    // declare a JSON document to hold the response
    const int capacity = JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(7);
    StaticJsonDocument<capacity> response;

    // get the current state of the light, whether it's on or off
    DeserializationError err = deserializeJson(response, packetBuffer);
    if (err.code() == DeserializationError::Ok) {
      lightOn = response["result"]["state"];
    }
  }
  return lightOn;
}

void loop() {
      Serial.println("Flashing lights");
      // flash the lights twice
      int i = 0;
      
      while (i < 2) {
        // turn all the lights off
        for (int i = 0; i < numLights; i++) {
          IPAddress lightIP = lights[i];
          Udp.beginPacket(lightIP, lightPort);
          Udp.write(OFF_BUFFER);
          Udp.endPacket();
        }
        // wait this long to turn the lights back on
        delay(500);
        
        // turn them back on
        for (int i = 0; i < numLights; i++) {
          
          IPAddress lightIP = lights[i];
          Serial.println(lightIP);
          sendColorCommand(lightIP, lightPort, millis() % 256,random(0, 256), random(0, 256), random(0, 256));
          Udp.beginPacket(lightIP, lightPort);
          Udp.write(ON_BUFFER);
          Udp.endPacket();
        }
        delay(500);
        i++;
      }
    
    // cooldown for 2 seconds
    Serial.println("Cooling down...");
    delay(2000);
 
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi module's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address of ESP8266: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}



void populateLights() {
  numLights = 0;
  for (int i = 0; i <= 254; i++) {
    IPAddress candidateIP = makeIP(i);
    if (isLightPresent(candidateIP, lightPort)) {
      lights[numLights++] = candidateIP;
      Serial.print("Light found at: ");
      Serial.println(candidateIP);
    }
  }
  Serial.print("Total lights found: ");
  Serial.println(numLights);
}

IPAddress makeIP(byte lastOctet) {
  return IPAddress(192, 168, 2, lastOctet);
}


void sendColorCommand(IPAddress lightIP, uint16_t port, uint8_t r, uint8_t g, uint8_t b, uint8_t dimming) {
  char buffer[200];

  snprintf(buffer, sizeof(buffer),
    "{\"method\":\"setPilot\",\"params\":{\"r\":%d,\"g\":%d,\"b\":%d,\"dimming\":%d}}",
    r, g, b, dimming);

  Udp.beginPacket(lightIP, port);
  Udp.write((const uint8_t*)buffer, strlen(buffer));
  Udp.endPacket();

  Serial.print("Sent: ");
  Serial.println(buffer);
}


void requestWizState(IPAddress wizIp, int wizPort) {
  String payload = R"({"method":"getPilot","params":{}})";
  Udp.beginPacket(wizIp, wizPort);
  Udp.write(payload.c_str());
  Udp.endPacket();
}



bool receiveWizState(WizDeviceInfo &state) {
  unsigned long startTime = millis();
  while (millis() - startTime < 1000) {
    int packetSize = Udp.parsePacket();
    if (packetSize) {
      char buffer[512];
      int len = Udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) buffer[len] = 0;

      StaticJsonDocument<512> doc;
      DeserializationError err = deserializeJson(doc, buffer);
      if (err) return false;

      JsonObject result = doc["result"];
      state.state = result["state"] | false;
      state.r = result["r"] | 0;
      state.g = result["g"] | 0;
      state.b = result["b"] | 0;
      state.brightness = result["dimming"] | 0;
      state.temp = result["temp"] | 0;
      state.scene = result["scene"] | "";
      state.effectId = result["effectId"] | 0;
      state.speed = result["speed"] | 0;


      return true;
    }
  }
  return false;  // Timed out
}

bool receiveWizSystemConfig(WizDeviceInfo &state) {
  unsigned long start = millis();
  while (millis() - start < 1000) {
    int packetSize = Udp.parsePacket();
    if (packetSize) {
      char buffer[512];
      int len = Udp.read(buffer, sizeof(buffer) - 1);
      buffer[len] = 0;

      StaticJsonDocument<512> doc;
      if (deserializeJson(doc, buffer)) return false;

      JsonObject result = doc["result"];
      state.mac = result["mac"] | "";
      state.friendlyName = result["friendlyName"] | "";
      state.fwVersion = result["fwVersion"] | "";
      state.moduleName = result["moduleName"] | "";
      return true;
    }
  }
  return false;
}


//requestWizSystemConfig(wizIp, wizPort, bulb);


/*      state.mac = result["mac"] | "";
      state.moduleName = result["moduleName"] | "";
      state.fwVersion = result["fwVersion"] | "";
      state.friendlyName = result["friendlyName"] | "";
      state.ip = wizIp; // Optional


     */

void requestWizSystemConfig(IPAddress wizIp, int wizPort) {
  String payload = R"({"method":"getSystemConfig","params":{}})";
  Udp.beginPacket(wizIp, wizPort);
  Udp.write(payload.c_str());
  Udp.endPacket();
}
