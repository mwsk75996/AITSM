/*
  ===========================================================
   Modbus TCP Server (Slave) – OLIMEX ESP32-POE
   Kompatibel med Siemens LOGO! PLC
  ===========================================================

  Funktioner:
  - Faste IP-adresser: ESP32: 192.168.137.11 / Gateway: 192.168.137.1
  - Flere coils og holding-registere
  - Kan styre Siemens LOGO! PLC via coil-write
  - Temperatur og counter simuleres i ESP32
  - Serial Monitor viser alle værdier
*/
/*
Hvordan det virker med Siemens LOGO! PLC

Konfigurer PLC’en som Modbus TCP-klient (master):
PLC IP: f.eks. 192.168.137.1
Slave IP: 192.168.137.11 (ESP32)
Port: 502

Læs/Hold registere i PLC:
Holding Register 0 → Temperatur
Holding Register 1 → Counter
Holding Register 2 → Output

Skriv coils fra PLC:
Coil 0 → aktiver HREG_OUTPUT = counter+100
Coil 1 → aktiver HREG_OUTPUT = counter*2
Serial Monitor på ESP32 viser alle aktuelle værdier, så du kan debugge nemt.
*/

#include <Arduino.h>
#include <ETH.h>
#include <ModbusIP_ESP8266.h>

// Modbus TCP server instans
ModbusIP mb;

// Modbus-registeradresser (Kan kortlægges til LOGO!)
const int COIL_1           = 0;   // Coil 0 – fx start/stop signal til LOGO!
const int COIL_2           = 1;   // Coil 1 – ekstra styringsbit
const int HREG_TEMPERATURE = 0;   // Holding register 0 – temperatur
const int HREG_COUNTER     = 1;   // Holding register 1 – tæller
const int HREG_OUTPUT      = 2;   // Holding register 2 – kan bruges til PLC output

uint16_t counter = 0;

// Ethernet faste IP
IPAddress localIP(192, 168.137.11);
IPAddress gateway(192, 168.137.1);
IPAddress subnet(255, 255, 255, 0);

// Ethernet-event handler (ESP32 core 3.x+)
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
  Serial.println("\n=== Starter Modbus TCP Server til LOGO! PLC ===");

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

  // Initialiser coils
  mb.addCoil(COIL_1, false);
  mb.addCoil(COIL_2, false);

  // Initialiser holding-registere
  mb.addHreg(HREG_TEMPERATURE, 25);   // Temperatur
  mb.addHreg(HREG_COUNTER, 0);        // Counter
  mb.addHreg(HREG_OUTPUT, 0);         // Output register til PLC

  Serial.println("Modbus-server klar på port 502. Klar til LOGO! PLC.\n");
}

void loop() {
  mb.task(); // Håndter Modbus-forespørgsler

  static uint32_t lastMillis = 0;
  if (millis() - lastMillis > 2000) {
    lastMillis = millis();

    // Simuler værdier
    counter++;
    uint16_t temp = 20 + random(0, 10);

    mb.Hreg(HREG_TEMPERATURE, temp);
    mb.Hreg(HREG_COUNTER, counter);

    // Læs coil-status fra PLC
    bool coil1 = mb.Coil(COIL_1);
    bool coil2 = mb.Coil(COIL_2);

    // Dynamisk reaktion: hvis coil1 ON, sæt HREG_OUTPUT til counter+100
    if (coil1) {
      mb.Hreg(HREG_OUTPUT, counter + 100);
    } else if (coil2) { // Hvis coil2 ON, sæt HREG_OUTPUT til counter*2
      mb.Hreg(HREG_OUTPUT, counter * 2);
    } else {
      mb.Hreg(HREG_OUTPUT, counter);
    }

    // Udskriv alle værdier til Serial Monitor
    Serial.printf("Temp: %d °C | Counter: %d | Coil1: %s | Coil2: %s | Output: %d\n",
                  mb.Hreg(HREG_TEMPERATURE),
                  counter,
                  coil1 ? "ON" : "OFF",
                  coil2 ? "ON" : "OFF",
                  mb.Hreg(HREG_OUTPUT));
  }
}
