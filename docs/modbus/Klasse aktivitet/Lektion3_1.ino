#include <Arduino.h>    // Grundlæggende Arduino funktioner
#include <ModbusRTU.h>  // Modbus RTU bibliotek (Alexander Emelianov)

// ============================================================
// ============= KONFIGURATION ===============================
// ============================================================

#define SLAVE_ID 1                 // ID på Modbus-slave enhed vi kommunikerer med
#define RS485_SERIAL_PORT Serial2  // UART-port til RS485 (ESP32 har flere hardware-serial)
#define RS485_DE_RE_PIN 4          // GPIO til DE/RE (Driver Enable / Receiver Enable) på RS485

ModbusRTU mb;  // Opretter en ModbusRTU master-instans

// ============================================================
// ============= CALLBACK FOR SKRIVNING ======================
// ============================================================

// Denne funktion bliver kaldt når en skrive-kommando (coil/register) er færdig
// event = resultatkode (success/failure), transactionId = unik transaktions-ID
bool cbWrite(Modbus::ResultCode event, uint16_t transactionId, void* data) {
  Serial.printf("Skrive-anmodning resultat: 0x%02X, Transaktion: %d\n", event, transactionId);

  if (event == Modbus::EX_SUCCESS) {
    Serial.println("Skrivning udført korrekt!");
  } else {
    Serial.println("Fejl under Modbus-skrivning.");
  }
  return true;  // Returner true for at indikere at callback er håndteret
}

// ============================================================
// ============= SETUP =======================================
// ============================================================

void setup() {
  Serial.begin(115200);  // Starter Serial Monitor til debugging
  delay(500);            // Kort pause for at sikre Serial Monitor er klar
  Serial.println("\n--- Starter Modbus RTU master ---");

  // Initialiser RS485 UART-port
  // RX = GPIO16, TX = GPIO17, 9600 baud, 8 data bits, ingen parity, 1 stop bit
  RS485_SERIAL_PORT.begin(19200, SERIAL_8N1, 36, 4);

  // Konfigurer DE/RE GPIO som OUTPUT
  // DE/RE styrer retningen på RS485: LOW = modtage, HIGH = sende
  pinMode(RS485_DE_RE_PIN, OUTPUT);
  digitalWrite(RS485_DE_RE_PIN, LOW);  // Start med modtage-tilstand

  // Initialiser Modbus master
  // &RS485_SERIAL_PORT = hvilken UART port, RS485_DE_RE_PIN = retningstyring
  mb.begin(&RS485_SERIAL_PORT, RS485_DE_RE_PIN);
  mb.master();  // Sæt ESP32 som Modbus master

  Serial.println("Modbus RTU master klar.");
}

// ============================================================
// ============= LOOP ========================================
// ============================================================

void loop() {
  static uint32_t lastMillis = 0;  // Gemmer sidste tidspunkt for forespørgsel

  // Kør kode hvert 5. sekund
  if (millis() - lastMillis > 5000) {
    lastMillis = millis();  // Opdater sidste tidspunkt
    Serial.println("\n--- Modbus forespørgsler ---");

    // ========================================================
    // LÆS EN COIL (bool)
    // En coil er 1-bit (ON/OFF)
    // ========================================================
    bool coilValue = false;                          // Variabel til coil-værdi
    if (mb.readCoil(SLAVE_ID, 0, &coilValue, 1)) {   // Læs coil 0 fra slave
      Serial.printf("Coil-værdi: %d\n", coilValue);  // Udskriv coil-værdi (0/1)
    } else {
      Serial.println("Fejl ved læsning af coil");
    }

    // ========================================================
    // LÆS HOLDING REGISTER (uint16_t)
    // Holding register er 16-bit værdier
    // ========================================================
    uint16_t regValue = 0;                                      // Variabel til register-værdi
    if (mb.readHreg(SLAVE_ID, 0, &regValue, 1)) {               // Læs register 0 fra slave
      Serial.printf("Holding register-værdi: %d\n", regValue);  // Udskriv værdi
    } else {
      Serial.println("Fejl ved læsning af holding register");
    }

    // ========================================================
    // SKRIV TIL COIL (skift tilstand)
    // ========================================================
    bool newCoilState = !coilValue;  // Skift coil-værdi (ON -> OFF eller OFF -> ON)
    if (mb.writeCoil(SLAVE_ID, 0, newCoilState, cbWrite)) {
      Serial.printf("Coil-skrivning sendt: %d\n", newCoilState);
    } else {
      Serial.println("Fejl ved afsendelse af coil-skrivning");
    }

    // ========================================================
    // SKRIV TIL HOLDING REGISTER
    // Skriv til holding register 0 på slave 1 med ny værdi
    // SLAVE_ID = 1 -> mål-slave
    // 0 = registeradresse (HR0)
    // newRegValue = den 16-bit værdi der skal skrives
    // cbWrite = callback-funktion der håndterer resultatet (success/fejl)
    // ========================================================
    uint16_t newRegValue = random(0, 100);  // Tilfældig testværdi 0-99
    if (mb.writeHreg(SLAVE_ID, 0, newRegValue, cbWrite)) {
      Serial.printf("Skrivning til holding register sendt: %d\n", newRegValue);
    } else {
      Serial.println("Fejl ved afsendelse af register-skrivning");
    }
  }

  // ========================================================
  // Modbus task:
  // Skal kaldes ofte for at håndtere Modbus-protokollen internt
  // ========================================================
  mb.task();
  yield();  // Giv CPU tid til baggrundsopgaver (esp32 multitasking)
}
