#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "config.h"


WiFiUDP udp;
ESP8266WebServer server(80);

const int udpPort = 38899;
const int maxBulbs = 10;
String bulbIPs[maxBulbs];
int bulbCount = 0;

unsigned long lastRaveTime = 0;
unsigned long raveInterval = 1000;  // milliseconds
bool raveEnabled = false;

// Simple RGB random color sender
void sendColor(const String& ip, int r, int g, int b) {
  StaticJsonDocument<256> doc;
  doc["method"] = "setState";
  JsonObject params = doc.createNestedObject("params");
  params["state"] = true;
  params["r"] = r;
  params["g"] = g;
  params["b"] = b;

  char buffer[256];
  size_t len = serializeJson(doc, buffer);

  udp.beginPacket(ip.c_str(), udpPort);
  udp.write((uint8_t*)buffer, len);
  udp.endPacket();
}

void discoverBulbs() {
  StaticJsonDocument<64> doc;
  doc["method"] = "getPilot";
  doc["params"] = JsonObject();  // empty

  char buffer[128];
  size_t len = serializeJson(doc, buffer);

  IPAddress ip = WiFi.localIP();
  IPAddress subnet = WiFi.subnetMask();
  IPAddress broadcast;
  
  for (int i = 0; i < 4; i++) {
    broadcast[i] = ip[i] | ~subnet[i];
  } 
  udp.beginPacket(broadcast, udpPort);
  udp.write((uint8_t*)buffer, len);
  udp.endPacket();

  unsigned long start = millis();
  udp.begin(udpPort);
  while (millis() - start < 3000) {
    int packetSize = udp.parsePacket();
    if (packetSize) {
      char reply[512];
      udp.read(reply, sizeof(reply));
      IPAddress remoteIP = udp.remoteIP();
      String ipStr = remoteIP.toString();

      // Avoid duplicates
      bool exists = false;
      for (int i = 0; i < bulbCount; i++) {
        if (bulbIPs[i] == ipStr) {
          exists = true;
          break;
        }
      }
      if (!exists && bulbCount < maxBulbs) {
        bulbIPs[bulbCount++] = ipStr;
        Serial.print("Discovered bulb at ");
        Serial.println(ipStr);
      }
    }
  }
  udp.stop();
}

void handleRoot() {
  String html = "<html><body><h1>Bulb Catalog</h1>";
  for (int i = 0; i < bulbCount; i++) {
    html += "<form method='GET' action='/name'>";
    html += "IP: " + bulbIPs[i] + "<br>";
    html += "Name: <input name='name'><input type='hidden' name='ip' value='" + bulbIPs[i] + "'>";
    html += "<input type='submit' value='Save'></form><br>";
  }
  html += "<a href='/rave?on=1'>Start Rave</a><br>";
  html += "<a href='/rave?on=0'>Stop Rave</a><br>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleName() {
  String name = server.arg("name");
  String ip = server.arg("ip");
  Serial.printf("💡 Saved name \"%s\" for bulb at %s\n", name.c_str(), ip.c_str());
  // In real project: save to EEPROM or file
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleRave() {
  String on = server.arg("on");
  raveEnabled = (on == "1");
  Serial.println(raveEnabled ? "🎉 Rave mode ON" : "🛑 Rave mode OFF");
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(wifi_ssid, wifi_password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected.");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  discoverBulbs();

  server.on("/", handleRoot);
  server.on("/name", handleName);
  server.on("/rave", handleRave);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  if (raveEnabled && millis() - lastRaveTime >= raveInterval) {
    lastRaveTime = millis();
    for (int i = 0; i < bulbCount; i++) {
      int r = random(256);
      int g = random(256);
      int b = random(256);
      sendColor(bulbIPs[i], r, g, b);
    }
  }
}
