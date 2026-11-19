/*
  ===========================================================
   Modbus TCP Klient (Master) til Olimex ESP32-POE
  ===========================================================

  Denne kode får din ESP32-POE til at fungere som en Modbus TCP-klient
  (også kaldet master). Den opretter forbindelse til en Modbus TCP-slave
  (typisk en simulator på din PC) og læser et register (Hreg 512)
  hvert 5. sekund.

  Testet med:
  - ESP32 core v3.x
  - Ethernet LAN8720 (Olimex ESP32-POE)
  - Bibliotek: modbus-esp8266 (af Alexander Emelianov)

  -----------------------------------------------------------
  Kræver:
  * Biblioteket: modbus-esp8266 (via Library Manager)
  * Ethernetforbindelse (PoE eller RJ45)
  * En aktiv Modbus TCP-slave på din PC (f.eks. Modbus Poll)
  -----------------------------------------------------------
*/

#include <ETH.h>              // Ethernet-håndtering for ESP32
#include <ModbusEthernet.h>   // ModbusTCP-bibliotek

// Ethernet-pin-konfiguration for OLIMEX ESP32-POE
#define ETH_PHY_ADDR      0
#define ETH_PHY_POWER_PIN 12
#define ETH_PHY_MDC_PIN   23
#define ETH_PHY_MDIO_PIN  18
#define ETH_TYPE          ETH_PHY_LAN8720
#define ETH_CLK_MODE      ETH_CLOCK_GPIO17_OUT

// Netværkskonfiguration (fast IP)
IPAddress localIP(192, 168, 137, 11);   // ESP32'ens IP-adresse
IPAddress gateway(192, 168, 137, 1);    // Din routers / PC'ens IP
IPAddress subnet(255, 255, 255, 0);

// Modbus TCP indstillinger
IPAddress slaveIP(192, 168, 137, 1);    // IP-adressen på din Modbus TCP-slave
const uint16_t SLAVE_PORT = 502;        // Standard Modbus TCP-port
const uint16_t REG_ADDR = 512;          // Register, der skal læses (Hreg 512)
uint16_t modbusValue = 0;               // Variabel til at gemme den læste værdi

// Opret ModbusTCP-klient instans
ModbusEthernet mb;

// Funktion der håndterer Ethernet-begivenheder
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Serial.println("Ethernet initialiseret");
      ETH.setHostname("esp32-poe-master");
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("Ethernet-forbindelse oprettet");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      Serial.print("ESP32 IP-adresse: ");
      Serial.println(ETH.localIP());
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println("Ethernet afbrudt");
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println("Ethernet stoppet");
      break;
    default:
      break;
  }
}

void setup() {
  // Start seriel kommunikation
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32-POE Modbus TCP Klient (Master) ===");

  // Initialiser Ethernet-interface
  WiFi.onEvent(WiFiEvent);  // Aktiver event-håndtering for netværket
  ETH.begin(ETH_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC_PIN, ETH_PHY_MDIO_PIN, ETH_PHY_POWER_PIN, ETH_CLK_MODE);
  ETH.config(localIP, gateway, subnet);  // Sæt fast IP-konfiguration

  // Vent lidt for at give Ethernet tid til at starte
  delay(2000);

  // Start Modbus-klient
  mb.client();
  Serial.println("Modbus-klient initialiseret. Forbinder til slave...");
}

uint32_t lastPoll = 0;  // Variabel til tidsstyring (hvert 5. sekund)

void loop() {
  // Kør Modbus-opgaver (skal kaldes ofte)
  mb.task();

  // Tjek om klienten allerede er forbundet til slaven
  if (!mb.isConnected(slaveIP)) {
    Serial.println("Forsøger at forbinde til Modbus-slave...");
    mb.connect(slaveIP, SLAVE_PORT); // Opret forbindelse
    delay(1000);
  }

  // Hver 5. sekund (5000 ms), læs registeret fra slaven
  if (millis() - lastPoll > 5000) {
    lastPoll = millis();
    if (mb.isConnected(slaveIP)) {
      Serial.println("Læser Modbus-register 512...");
      // Læs Holding Register (funktion 03)
      if (!mb.readHreg(slaveIP, REG_ADDR, &modbusValue)) {
        Serial.println("Forespørgsel mislykkedes, prøver igen...");
      }
    } else {
      Serial.println("Ingen forbindelse til Modbus-slave endnu.");
    }
  }

  // Når Modbus-svaret er klar, vis resultatet
  static uint16_t lastValue = 0;
  if (modbusValue != lastValue) {
    Serial.print("Ny Modbus-værdi modtaget fra slave: ");
    Serial.println(modbusValue);
    lastValue = modbusValue;
  }

  delay(50);  // Lille pause for stabilitet
}
