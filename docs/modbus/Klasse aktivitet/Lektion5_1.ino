/*
  ModbusTCP-klient til OLIMEX ESP32-POE (LAN8720 Ethernet)
  --------------------------------------------------------
  Denne kode viser, hvordan ESP32-POE kan bruges som Modbus TCP-klient
  via det indbyggede Ethernet-interface (LAN8720 PHY).

  - ESP32-POE læser og kan skrive Modbus registre fra en Modbus TCP-slave.
  - Kræver biblioteket: "modbus-esp8266" af Alexander Emelianov
    https://github.com/emelianov/modbus-esp8266
Din PC (hvor simulatoren kører) skal være på samme netværk som ESP32-POE.
Eksempel:
PC / simulator: 192.168.137.1
ESP32-POE: 192.168.137.11
De skal kunne pinges fra hinanden.
*/

#include <ETH.h>                 // ESP32’s indbyggede Ethernet-driver
#include <ModbusIP_ESP8266.h>    // Modbus TCP/IP bibliotek (virker også på ESP32)
#include <Arduino.h>

// Opret en Modbus TCP-klientinstans
ModbusIP mb;

// IP-adresser (tilpas til dit lokale netværk)
const IPAddress slaveIP(192, 168, 137, 1);   // IP-adresse på Modbus-slave
const IPAddress localIP(192, 168, 137, 11);  // IP-adresse til ESP32-POE
const IPAddress gateway(192, 168, 137, 1);   // Gateway/router
const IPAddress subnet(255, 255, 255, 0);    // Netmaske

// Modbus-parametre
const uint16_t REG_READ = 0;    // Register der skal læses
const uint16_t REG_WRITE = 1;   // Register der skal skrives til
uint16_t læstVærdi = 0;           // Variabel til at gemme Modbus-værdi

// Ethernet PHY-konfiguration (fast for Olimex ESP32-POE)
#define ETH_PHY_ADDR        0
#define ETH_PHY_POWER_PIN   12
#define ETH_MDC_PIN         23
#define ETH_MDIO_PIN        18
#define ETH_TYPE            ETH_PHY_LAN8720
#define ETH_CLK_MODE        ETH_CLOCK_GPIO17_OUT

// ---------------------------------------------------------------------------
// Event-håndtering for Ethernet (ESP32 Arduino core v3.x)
// ---------------------------------------------------------------------------
// Disse hændelser fanges automatisk, når Ethernet starter, får IP, kobles fra osv.
void onEthEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("Ethernet startet");
      ETH.setHostname("esp32-poe-modbus");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("Ethernet forbundet til netværket");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("IP-adresse tildelt: ");
      Serial.println(ETH.localIP());
      mb.client();  // Start Modbus-klient, når der er IP
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("Ethernet frakoblet");
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("Ethernet stoppet");
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Opsætning
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n Starter Modbus TCP-klient for ESP32-POE...");

  // Registrér event-callback for Ethernet-hændelser
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    onEthEvent(event);
  });

  // Start Ethernet-forbindelse med faste IP-indstillinger
  ETH.begin(ETH_TYPE, ETH_PHY_ADDR, ETH_MDC_PIN, ETH_MDIO_PIN,
            ETH_PHY_POWER_PIN, ETH_CLK_MODE);
  ETH.config(localIP, gateway, subnet);

  Serial.println("Initialiserer Ethernet...");
}

// ---------------------------------------------------------------------------
// Hovedloop
// ---------------------------------------------------------------------------
void loop() {
  mb.task();  // Kør Modbus’ baggrundsopgaver

  // Hvis Ethernet-linket er aktivt (kablet er sat i)
  if (ETH.linkUp()) {
    // Hvis der allerede er forbindelse til Modbus-slaven
    if (mb.isConnected(slaveIP)) {
      // Læs et holding-register (eksempel)
      mb.readHreg(slaveIP, REG_READ, &læstVærdi);

      // Skriv en værdi til et andet register (eksempel)
      uint16_t nyVærdi = læstVærdi + 1;  // fx bare for test
      mb.writeHreg(slaveIP, REG_WRITE, nyVærdi);

    } else {
      // Forsøg at forbinde, hvis ikke allerede forbundet
      Serial.println("Forsøger at oprette forbindelse til Modbus-slave...");
      mb.connect(slaveIP);
    }
  }

  // Udskriv status hvert 5. sekund
  static uint32_t sidsteVisning = 0;
  if (millis() - sidsteVisning > 5000) {
    sidsteVisning = millis();
    Serial.print("Læst værdi fra REG ");
    Serial.print(REG_READ);
    Serial.print(": ");
    Serial.println(læstVærdi);
  }

  delay(100);
}
