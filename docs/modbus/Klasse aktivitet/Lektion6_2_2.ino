/*
  ===========================================================
   Modbus TCP Server (Slave) – OLIMEX ESP32-POE
   Reagerer dynamisk på coil-write og opdaterer registre
  ===========================================================

  Funktioner:
  - Faste IP-adresser: 192.168.137.11 / Gateway 192.168.137.1
  - Opdaterer temperatur og counter hvert 2. sekund
  - Reagerer på coil-write fra klient (tænder/slukker LED)
  - Viser alle værdier i Serial Monitor
*/
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

Dynamisk coil-react:
Hvis klient skriver Coil 0 = ON, så lægges +100 til counter-registeret.
Hvis Coil = OFF, counter fortsætter normalt.

Serial Monitor:
Viser temperatur, counter og coil-status hvert 2. sekund.
Test med Modbus-klient
Start Modbus Poll eller QModMaster.
Forbind til 192.168.137.11, port 502.

Læs:
Holding Register 0 → Temperatur
Holding Register 1 → Counter
Skriv Coil 0 → ON/OFF, se counter ændre sig dynamisk i Serial Monitor.

Test med Modbus-klient:
Start Modbus Poll eller QModMaster.
Forbind til 192.168.137.11, port 502.
Læs:
Holding Register 0 → Temperatur
Holding Register 1 → Counter
Skriv Coil 0 → ON/OFF, se counter ændre sig dynamisk i Serial Monitor.

*/


#include <Arduino.h>
#include <ETH.h>
#include <ModbusIP_ESP8266.h>

// Modbus TCP server instans
ModbusIP mb;

// Modbus-registeradresser
const int COIL_1           = 0;  // Coil (ON/OFF)
const int HREG_TEMPERATURE = 0;  // Temperatur
const int HREG_COUNTER     = 1;  // Tæller

uint16_t counter = 0;  // Tæller for demo

// Ethernet faste IP
IPAddress localIP(192, 168, 137, 11);
IPAddress gateway(192, 168, 137, 1);
IPAddress subnet(255, 255, 255, 0);

// Ethernet-event handler
void WiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("📡 Ethernet startet");
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
  Serial.println("\n=== Starter Modbus TCP Server med dynamisk coil-react ===");

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
  // Håndter Modbus-forespørgsler
  mb.task();

  // Opdater registre hvert 2. sekund
  static uint32_t lastMillis = 0;
  if (millis() - lastMillis > 2000) {
    lastMillis = millis();

    // Simuler temperatur og tæller
    counter++;
    uint16_t temp = 20 + random(0, 10);

    mb.Hreg(HREG_TEMPERATURE, temp);
    mb.Hreg(HREG_COUNTER, counter);

    // Læs coil-status (kan ændres af klient)
    bool coilState = mb.Coil(COIL_1);

    // Dynamisk reaktion: hvis coil ON, læg 100 til counter
    if (coilState) {
      mb.Hreg(HREG_COUNTER, counter + 100);
    }

    // Udskriv værdier til Serial Monitor
    Serial.printf("Temp: %d °C | Counter: %d | Coil: %s\n",
                  mb.Hreg(HREG_TEMPERATURE),
                  mb.Hreg(HREG_COUNTER),
                  coilState ? "ON" : "OFF");
  }
}
