/*
ESP32-POE modtage forespørgsler fra din Modbus-client-simulator.

*/

#include <ETH.h>
#include <ModbusIP_ESP8266.h>
#include <Arduino.h>

ModbusIP mb;  // ESP32 som Modbus TCP-server

#define REG_HOLDING 512  // Register der kan læses/skrives

#define ETH_PHY_ADDR        0
#define ETH_PHY_POWER_PIN   12
#define ETH_MDC_PIN         23
#define ETH_MDIO_PIN        18
#define ETH_TYPE            ETH_PHY_LAN8720
#define ETH_CLK_MODE        ETH_CLOCK_GPIO17_OUT

const IPAddress local_ip(192, 168, 137, 11);
const IPAddress gateway(192, 168, 137, 1);
const IPAddress subnet(255, 255, 255, 0);

void onEthEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ESP32-POE IP: ");
      Serial.println(ETH.localIP());
      break;
    default: break;
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    onEthEvent(event);
  });

  ETH.begin(ETH_TYPE, ETH_PHY_ADDR, ETH_MDC_PIN, ETH_MDIO_PIN,
            ETH_PHY_POWER_PIN, ETH_CLK_MODE);
  ETH.config(local_ip, gateway, subnet);

  // Opret et holding register
  mb.addHreg(REG_HOLDING, 123);
}

void loop() {
  mb.task();
  // Ændr registerværdi som test
  static uint32_t last = 0;
  if (millis() - last > 5000) {
    last = millis();
    uint16_t val = mb.Hreg(REG_HOLDING);
    mb.Hreg(REG_HOLDING, val + 1);
    Serial.print("Registerværdi = ");
    Serial.println(val + 1);
  }
}
