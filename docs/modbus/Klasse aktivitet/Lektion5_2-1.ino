/*
Hvad denne kode gør:
Ethernet med faste IP-adresser
ESP32: 192.168.137.11
Gateway: 192.168.137.1
Subnet: 255.255.255.0

Modbus TCP-server (slave):
Port 502
Coil 0 = ON/OFF
Holding register 0 = Temperatur (simuleret)
Holding register 1 = Counter

*/
#include <Arduino.h>
#include <ETH.h>
#include <ModbusIP_ESP8266.h>

// Opret Modbus-server
ModbusIP mb;

// Modbus-registeradresser
const int COIL_1           = 0;
const int HREG_TEMPERATURE = 0;
const int HREG_COUNTER     = 1;

uint16_t counter = 0;

// Ethernet faste IP
IPAddress localIP(192, 168, 137, 11);
IPAddress gateway(192, 168, 137, 1);
IPAddress subnet(255, 255, 255, 0);

// Ethernet-event handler med korrekt event-navne for ESP32 core 3.x+
void WiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("Ethernet startet");
      ETH.setHostname("esp32-poe-server");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("Ethernet tilsluttet");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("IP-adresse tildelt: ");
      Serial.println(ETH.localIP());
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("Ethernet afbrudt");
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("Ethernet stoppet");
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Starter Modbus TCP Server på ESP32-POE ===");

  // Start Ethernet med faste IP
  WiFi.onEvent(WiFiEvent);
  ETH.begin();
  ETH.config(localIP, gateway, subnet);
  delay(3000);

  if (ETH.linkUp()) {
    Serial.print("Forbundet med IP: ");
    Serial.println(ETH.localIP());
  } else {
    Serial.println("Ethernet-link ikke aktivt!");
  }

  // Start Modbus-server
  mb.server();

  // Initialiser coil og registre
  mb.addCoil(COIL_1, false);
  mb.addHreg(HREG_TEMPERATURE, 25);
  mb.addHreg(HREG_COUNTER, 0);

  Serial.println("Modbus-server klar på port 502.\n");
}

void loop() {
  mb.task();

  static uint32_t lastMillis = 0;
  if (millis() - lastMillis > 2000) {
    lastMillis = millis();

    // Opdater værdier
    counter++;
    uint16_t temp = 20 + random(0, 10);

    mb.Hreg(HREG_TEMPERATURE, temp);
    mb.Hreg(HREG_COUNTER, counter);

    bool coilState = mb.Coil(COIL_1);

    Serial.printf("Temp: %d °C | Counter: %d | Coil: %s\n",
                  mb.Hreg(HREG_TEMPERATURE),
                  counter,
                  coilState ? "ON" : "OFF");
  }
}
