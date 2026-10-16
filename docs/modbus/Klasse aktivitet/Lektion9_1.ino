/*
Funktionalitet:
Webinterface på http://192.168.137.11 med:
Læs holding registre, coils, discrete inputs
Skriv single register eller coil
Moderne, responsivt design
Modbus TCP gateway kommunikerer med slave på 192.168.137.1:502
Serial monitor viser status og kan udvides med menu
Automatisk håndtering af TCP-forbindelser og timeouts
Åbn browseren på http://192.168.137.11 for at styre Modbus via web
*/
#include <ETH.h>
#include <WiFi.h>
#include <WebServer.h>

// LAN8720 Ethernet PHY konfiguration
#define ETH_PHY_ADDR 1
#define ETH_PHY_TYPE ETH_PHY_LAN8720
#define ETH_PHY_POWER 17
#define ETH_PHY_MDC 23
#define ETH_PHY_MDIO 18
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN

// Statisk IP-konfiguration for ESP32
IPAddress local_IP(192, 168, 137, 11);
IPAddress gateway(192, 168, 137, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 137, 1);
IPAddress secondaryDNS(8, 8, 8, 8);

// Modbus TCP slave konfiguration
IPAddress modbusSlaveIP(192, 168, 137, 1);
const uint16_t modbusSlavePort = 502;

// Globale variabler
bool eth_connected = false;
WiFiClient modbusClient;
WebServer server(80);  // Webserver på port 80

// === MODBUS FUNKTIONER ===
uint16_t SerialReadUInt16() {
  while (!Serial.available()) delay(10);
  return (uint16_t)Serial.parseInt();
}

bool modbusSendRequest(uint8_t funcCode, uint16_t startAddr, uint16_t quantity, uint16_t value = 0, bool isWriteSingleCoil = false) {
  if (!modbusClient.connected()) {
    if (!modbusClient.connect(modbusSlaveIP, modbusSlavePort)) {
      return false;
    }
  }

  uint8_t packet[12] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, funcCode,
    highByte(startAddr), lowByte(startAddr), 0, 0
  };

  if (funcCode == 0x05 && isWriteSingleCoil) {
    uint16_t coilVal = value ? 0xFF00 : 0x0000;
    packet[10] = highByte(coilVal); packet[11] = lowByte(coilVal);
  } else if (funcCode == 0x06) {
    packet[10] = highByte(value); packet[11] = lowByte(value);
  } else {
    packet[10] = highByte(quantity); packet[11] = lowByte(quantity);
  }

  modbusClient.write(packet, 12);
  modbusClient.flush();
  return true;
}

String modbusReadResponse(uint16_t expectedCount, uint8_t funcCode) {
  long start = millis();
  int expectedBytes = 9 + ((funcCode == 0x01 || funcCode == 0x02) ? (expectedCount + 7) / 8 : expectedCount * 2);

  while (modbusClient.available() < expectedBytes && (millis() - start) < 2000) {
    delay(10);
  }

  if (modbusClient.available() < expectedBytes) return "Timeout";

  uint8_t buffer[256];
  int len = modbusClient.read(buffer, modbusClient.available());

  if (len < expectedBytes || (buffer[7] & 0x80)) {
    return "Modbus Error";
  }

  String result = "";
  if (funcCode == 0x01 || funcCode == 0x02) {
    for (uint16_t i = 0; i < expectedCount; i++) {
      uint8_t byteIdx = i / 8, bitIdx = i % 8;
      bool state = buffer[9 + byteIdx] & (1 << bitIdx);
      result += state ? "1" : "0";
      if (i < expectedCount - 1) result += ",";
    }
  } else {
    for (uint16_t i = 0; i < expectedCount; i++) {
      uint16_t val = (buffer[9 + i*2] << 8) | buffer[10 + i*2];
      result += String(val);
      if (i < expectedCount - 1) result += ",";
    }
  }
  return result;
}

// === WEBSERVER ENDPOINTS ===
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head><title>ESP32 Modbus Gateway</title>
<style>
body{font-family:Arial;margin:40px;background:#f0f0f0;}
.container{max-width:800px;margin:auto;background:white;padding:30px;border-radius:10px;box-shadow:0 0 20px rgba(0,0,0,0.1);}
h1{text-align:center;color:#333;}
.form-group{margin:20px 0;}
label{display:block;margin-bottom:5px;font-weight:bold;}
input,select{width:100%;padding:10px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;}
button{background:#007bff;color:white;padding:12px 24px;border:none;border-radius:5px;cursor:pointer;font-size:16px;width:100%;margin-top:10px;}
button:hover{background:#0056b3;}
.result{background:#e8f5e8;padding:15px;border-radius:5px;margin-top:15px;display:none;}
.error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb;}
.success{background:#d4edda;color:#155724;border:1px solid #c3e6cb;}
</style></head>
<body>
<div class="container">
<h1>ESP32 Modbus TCP Gateway</h1>
<p><strong>ESP32 IP:</strong> )rawliteral" + ETH.localIP().toString() + R"rawliteral( | <strong>Modbus Slave:</strong> )rawliteral" + modbusSlaveIP.toString() + R"rawliteral( :502</p>

<div class="form-group">
<h3>Læs Data</h3>
<label>Funktion:</label>
<select id="func">
<option value="3">Holding Registers (FC3)</option>
<option value="1">Coils (FC1)</option>
<option value="2">Discrete Inputs (FC2)</option>
</select>
<label>Start Adresse:</label><input type="number" id="startAddr" value="0">
<label>Antal:</label><input type="number" id="quantity" value="5">
<button onclick="readData()">Læs Data</button>
<div id="readResult" class="result"></div>
</div>

<div class="form-group">
<h3>Skriv Data</h3>
<label>Type:</label>
<select id="writeFunc">
<option value="6">Holding Register (FC6)</option>
<option value="5">Single Coil (FC5)</option>
</select>
<label>Adresse:</label><input type="number" id="writeAddr" value="0">
<label>Værdi:</label><input type="number" id="writeValue" value="123">
<button onclick="writeData()">Skriv Data</button>
<div id="writeResult" class="result"></div>
</div>
</div>

<script>
function showResult(id, msg, isError) {
  const el = document.getElementById(id);
  el.textContent = msg;
  el.className = 'result ' + (isError ? 'error' : 'success');
  el.style.display = 'block';
}

async function readData() {
  const func = document.getElementById('func').value;
  const addr = document.getElementById('startAddr').value;
  const qty = document.getElementById('quantity').value;
  
  try {
    const response = await fetch(`/read?func=${func}&addr=${addr}&qty=${qty}`);
    const data = await response.text();
    showResult('readResult', `Resultat: ${data}`, false);
  } catch(e) {
    showResult('readResult', 'Fejl: ' + e.message, true);
  }
}

async function writeData() {
  const func = document.getElementById('writeFunc').value;
  const addr = document.getElementById('writeAddr').value;
  const val = document.getElementById('writeValue').value;
  
  try {
    const response = await fetch(`/write?func=${func}&addr=${addr}&val=${val}`);
    const data = await response.text();
    showResult('writeResult', data, false);
  } catch(e) {
    showResult('writeResult', 'Fejl: ' + e.message, true);
  }
}
</script>
</body></html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void handleRead() {
  if (!server.hasArg("func") || !server.hasArg("addr") || !server.hasArg("qty")) {
    server.send(400, "text/plain", "Manglende parametre");
    return;
  }

  uint8_t func = server.arg("func").toInt();
  uint16_t addr = server.arg("addr").toInt();
  uint16_t qty = server.arg("qty").toInt();

  if (modbusSendRequest(func, addr, qty)) {
    String result = modbusReadResponse(qty, func);
    server.send(200, "text/plain", result);
  } else {
    server.send(500, "text/plain", "Modbus forbindelse fejlede");
  }
}

void handleWrite() {
  if (!server.hasArg("func") || !server.hasArg("addr") || !server.hasArg("val")) {
    server.send(400, "text/plain", "Manglende parametre");
    return;
  }

  uint8_t func = server.arg("func").toInt();
  uint16_t addr = server.arg("addr").toInt();
  uint16_t val = server.arg("val").toInt();

  bool isCoil = (func == 0x05);
  if (modbusSendRequest(func, addr, 0, val, isCoil)) {
    modbusWriteResponse(modbusClient);
    server.send(200, "text/plain", "Skrivning OK");
  } else {
    server.send(500, "text/plain", "Modbus skrivning fejlede");
  }
}

void modbusWriteResponse(WiFiClient &client) {
  long start = millis();
  while (client.available() < 12 && (millis() - start) < 2000) delay(10);
  if (client.available()) client.read(); // Read echo response
}

// === SERIAL MENU FUNKTIONER (som tidligere) ===
void onEthernetEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch(event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("Ethernet startet");
      ETH.setHostname("esp32-modbus-gateway");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("Ethernet forbundet");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("Ethernet IP: ");
      Serial.println(ETH.localIP());
      Serial.println("Webserver startet på http://" + ETH.localIP().toString());
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("Ethernet afbrudt");
      eth_connected = false;
      break;
    default: break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Network.onEvent(onEthernetEvent);
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLK_MODE);
  ETH.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);

  // Webserver routes
  server.on("/", handleRoot);
  server.on("/read", handleRead);
  server.on("/write", handleWrite);
  server.begin();
  Serial.println("Webserver klar");
}

void loop() {
  server.handleClient();
  
  if (!eth_connected) {
    delay(1000);
    return;
  }
  
  // Serial menu her hvis ønsket (kan udvides)
  delay(10);
}
