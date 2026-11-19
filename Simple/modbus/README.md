# Modbus TCP mellem to ESP32'er (#93)

Sideprojekt for at lære Modbus. Det er **ikke en del af afleveringen** og har
intet med Thingy-firmwaren at gøre (se [`README.md`](../../README.md#teknologistak)).

```
DS18B20 ──1-Wire, GPIO 4──▶ ESP32 slave ──Modbus TCP──▶ ESP32 master ──WiFi──▶ telefonens hotspot
                            192.168.4.10   (WiFi "aitsm-modbus")  192.168.4.1        (senere: VPS)
```

- **Slaven** (Modbus-server) måler temperaturen hvert 5. sekund og lægger den i
  input-registre.
- **Masteren** (Modbus-client) læser registrene hvert 5. sekund. Den er
  forbundet til telefonens hotspot og laver samtidig sit eget WiFi,
  `aitsm-modbus`, som slaven forbinder til. Afsendelse til VPS'en kommer
  senere.

## Hvorfor masteren laver sit eget WiFi

Med begge ESP32'er på telefonens hotspot virkede Modbus kun i under et minut ad
gangen. Derefter kom svarene flere sekunder for sent eller slet ikke, og
mDNS-opslag fejlede. Det skete med to forskellige Modbus-biblioteker, med og
uden sensor, og med WiFi-strømbesparelse slået fra. Hotspottet leverer altså
ikke trafik pålideligt mellem to enheder på det.

Med masterens eget access point går Modbus-trafikken direkte mellem de to
ESP32'er. Det giver også et lukket net til Modbus, hvilket er en fordel, fordi
Modbus TCP hverken har login eller kryptering (IEC 62443 CR 5.1, se
[redegørelsen](../../docs/standarder-iec62443-cra.md)).

ESP32 har kun én radio, så masterens access point kører altid på samme kanal
som hotspottet. Skifter telefonen kanal, forbinder slaven selv igen.

## Hardware

| | |
| --- | --- |
| Boards | 2 × ESP32-D0WD-V3 (ESP32 DevKit, `esp32dev`) |
| Sensor | DS18B20 på slaven: VDD til 3,3 V, GND, DQ til GPIO 4 |
| Pull-up | 4,7 kΩ mellem DQ og 3,3 V (sidder ofte på færdige moduler) |

DS18B20 forsynes med 3,3 V og ikke 5 V, fordi ESP32's GPIO ikke tåler 5 V.
GPIO 4 er valgt, fordi den ikke er en strapping-pin.

## Registerkort

Input-registre (funktionskode 04), unit id 1, port 502. Defineret i
[`include/modbus_map.h`](include/modbus_map.h), som begge sider bruger.

| Adresse | Indhold | Enhed |
| --- | --- | --- |
| 0 | Temperatur, signed | 0,01 °C (2345 = 23,45 °C) |
| 1 | Sensorstatus | 1 = gyldig måling, 0 = fejl |
| 2 | Antal målinger | Tæller, starter forfra efter 65535 |

Tælleren viser masteren, at værdien er ny og ikke en gammel måling.

## Filer

| Fil | Ansvar |
| --- | --- |
| `platformio.ini` | Ét projekt med to miljøer: `slave` og `master` |
| `include/modbus_map.h` | Netværk, port, unit id og registeradresser |
| `include/wifi_credentials.h.example` | Skabelon til WiFi-navne og -koder |
| `src/common/wifi_connect.cpp/.h` | Forbind som station og start access point |
| `src/slave/main.cpp` | Måleløkke og Modbus-server (eModbus) |
| `src/slave/temperature_sensor.cpp/.h` | DS18B20 uden blokering under målingen |
| `src/master/main.cpp` | WiFi, access point og Modbus-client (eModbus) |

eModbus svarer på forespørgsler i sin egen FreeRTOS-task. Slavens registre
deles derfor mellem den task og `loop()` og er beskyttet med en spinlock.

## Build og flash

Kræver [PlatformIO](https://platformio.org/). Kopiér først skabelonen og udfyld
hotspottets navn og kode og en kode til `aitsm-modbus` (mindst 8 tegn).
`wifi_credentials.h` er i `.gitignore`.

```bash
cd Simple/modbus
cp include/wifi_credentials.h.example include/wifi_credentials.h
pio run                                                   # bygger begge
pio run -e master -t upload --upload-port /dev/ttyUSB1
pio run -e slave -t upload --upload-port /dev/ttyUSB0
pio device monitor -p /dev/ttyUSB1                        # masterens log
```

Portene afhænger af, hvilken USB-port boardet sidder i. Slaven er den, der
skriver `DS18B20 fundet` ved opstart.

## Verifikation

Kørt 2026-11-19 med begge boards på samme bord:

```
[master] WiFi forbundet, IP 172.16.97.95, kanal 11
[master] Access point "aitsm-modbus" startet, IP 192.168.4.1, kanal 11
[slave]  WiFi forbundet, IP 192.168.4.10, kanal 11
[slave]  Modbus TCP-server på port 502, unit id 1
[slave]  Måling 5: 21.50 C
[master] Temperatur fra slaven: 21.50 C (måling 5)
```

23 af 23 forespørgsler over 2 minutter lykkedes uden fejl. Med begge boards på
telefonens hotspot lykkedes de første 9, hvorefter alle gav timeout.

## Videre arbejde

- Masteren sender temperaturen til VPS'en som SparkplugB over MQTT/TLS. Masteren
  bliver Edge Node og slaven Device (`spBv1.0/aitsm/DDATA/<edge node>/<device>`),
  se [`docs/sparkplug.md`](../../docs/sparkplug.md).
- Egen MQTT-bruger og ACL til masteren på VPS'en.
