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
const unsigned int LIGHT_PORT = 38899;      // the UDP port the lights are listening on; change this to fit your lights

// The static IPs of the lights
// Change these to match the (preferably static) IPs of the WiZ-enabled lights you're using.
// You can use any arbitrary number of lights and the program will work the same.
IPAddress LIGHTS[256]; // max possible lights
int numLights = 0;   

const char GET_BUFFER[] = "{\"method\":\"getPilot\"}"; // packet to get the state of a light
const char ON_BUFFER[] = "{\"method\":\"setPilot\",\"params\":{\"state\": 1}}";       // packet to turn off a light
const char OFF_BUFFER[] = "{\"method\":\"setPilot\",\"params\":{\"state\": 0}}";       // packet to turn off a light
char packetBuffer[255];

WiFiUDP Udp;

void setup() {
  // Set the motion sensor's pin to input mode
  pinMode(MOTION_PIN, INPUT);

  //Initialize serial
  Serial.begin(115200);

  Serial.print("Targeting the following lights on port: ");
  Serial.println(LIGHT_PORT);

 
 
  // attempt to connect to Wifi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(wifi_ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(wifi_ssid, wifi_password);

    // wait 5 seconds for connection:
    delay(5000);
  }

  Serial.println("Connected to WiFi");
  printWifiStatus();

  Serial.println("Waiting for motion...");
  Udp.begin(LOCAL_PORT);
  populateLights();
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
    IPAddress lightIP = LIGHTS[i];
    Serial.println(lightIP);
    if (!isLightOn(lightIP, LIGHT_PORT)) {
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
          IPAddress lightIP = LIGHTS[i];
          Udp.beginPacket(lightIP, LIGHT_PORT);
          Udp.write(OFF_BUFFER);
          Udp.endPacket();
        }
        // wait this long to turn the lights back on
        delay(500);
        
        // turn them back on
        for (int i = 0; i < numLights; i++) {
          IPAddress lightIP = LIGHTS[i];
          Udp.beginPacket(lightIP, LIGHT_PORT);
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
    if (isLightPresent(candidateIP, LIGHT_PORT)) {
      LIGHTS[numLights++] = candidateIP;
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
