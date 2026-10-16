#include <Arduino.h>
#include <ETH.h>
#include <WebServer.h>
#include <ModbusRTU.h>
#include <ModbusIP_ESP8266.h>

#define RXD2 16
#define TXD2 17
#define DE_RE_PIN 5
#define RTU_SLAVE_ID 1

ModbusRTU mbRTU;
ModbusIP mbTCP;
WebServer server(80);

uint16_t sensorValue = 0; // Modbus-registerværdi fra RTU

// Webside HTML
String htmlPage() {
  String page = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  page += "<meta http-equiv='refresh' content='3'>";  // auto-opdatering
  page += "<title>ESP32-PoE Modbus Gateway</title></head><body>";
  page += "<h1> ESP32-PoE Modbus Gateway</h1>";
  page += "<p><b>IP-adresse:</b> " + ETH.localIP().toString() + "</p>";
  page += "<p><b>RTU Slave ID:</b> " + String(RTU_SLAVE_ID) + "</p>";
  page += "<h2>Live Data</h2>";
  page += "<p>Holding Register [0] = <b>" + String(sensorValue) + "</b></p>";
  page += "<hr><p>Auto-opdatering hvert 3. sekund</p>";
  page += "</body></html>";
  return page;
}

void handleRoot() {
  server.send(200, "text/html", htmlPage());
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starter Modbus Gateway med Webserver...");

  // Start Ethernet
  ETH.begin();
  delay(3000);
  if (ETH.linkUp()) {
    Serial.print("Ethernet aktiv, IP: ");
    Serial.println(ETH.localIP());
  }

  // Start RS485 (Modbus RTU)
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  pinMode(DE_RE_PIN, OUTPUT);
  mbRTU.begin(&Serial2, DE_RE_PIN);
  mbRTU.master();

  // Start Modbus TCP-server
  mbTCP.server();

  // Start webserver
  server.on("/", handleRoot);
  server.begin();
  Serial.println("Webserver kører på port 80");
}

void loop() {
  mbRTU.task();
  mbTCP.task();
  server.handleClient();

  static uint32_t lastMillis = 0;
  if (millis() - lastMillis > 3000) {
    lastMillis = millis();

    uint16_t regValue;
    if (mbRTU.readHreg(RTU_SLAVE_ID, 0, &regValue, 1)) {
      sensorValue = regValue;
      mbTCP.addHreg(0, regValue);  // Tilgængelig via TCP
      Serial.printf(" RTU register[0] = %d\n", regValue);
    } else {
      Serial.println(" Fejl ved RTU-læsning");
    }
  }
}
