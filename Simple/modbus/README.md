# Modbus TCP mellem to ESP32'er (#93)

Sideprojekt for at lære Modbus. Det er **ikke en del af afleveringen** og har
intet med Thingy-firmwaren at gøre (se [`README.md`](../../README.md#teknologistak)).

```
DS18B20 ──1-Wire──▶ ESP32 slave ──Modbus TCP──▶ ESP32 master ──hotspot, MQTT/TLS + SparkplugB──▶ VPS
            GPIO 4  192.168.4.10  WiFi "aitsm-modbus"  192.168.4.1
                    Device: esp32-modbus               Edge Node: esp32-gateway
```

- **Slaven** (Modbus-server) måler temperaturen hvert 5. sekund og lægger den i
  input-registre.
- **Masteren** (Modbus-client) læser registrene hvert 5. sekund og gemmer
  målingerne med tid fra NTP. Den er forbundet til telefonens hotspot og laver
  samtidig sit eget WiFi, `aitsm-modbus`, som slaven forbinder til.
- **Til VPS'en** sender masteren målingerne som SparkplugB over MQTT/TLS (port
  8883). Den abonnerer på Thingy'ens NDATA og sender sin egen batch lige efter,
  så de to enheders batches kommer ind sammen.

## SparkplugB

| Rolle | Id | Beskeder |
| --- | --- | --- |
| Edge Node (master) | `esp32-gateway` | `NBIRTH` ved hver forbindelse, `NDEATH` som last will |
| Device (slave) | `esp32-modbus` | `DBIRTH` ved hver forbindelse, `DDATA` med målinger |

- `NDEATH` har samme `bdSeq` som NBIRTH. Brokeren sender den, hvis
  forbindelsen ryger uden disconnect, og en ny forbindelse får næste `bdSeq`.
- `DDATA` har én `temperature`-metric (Float, °C) pr. måling med målingens
  eget tidsstempel, markeret `is_historical`. Højst 60 målinger pr. besked.
- **Hvornår der sendes:** når en NDATA fra `thingy91x` er set, sendes hele
  bufferen. Kommer der ingen batch fra Thingy'en, sendes alligevel, når
  bufferen er tre fjerdedele fuld (90 målinger, 7,5 minut). Bufferen rummer
  120 målinger (10 minutter); er den fuld, springes nye målinger over og tælles.
- Målingerne fjernes først fra bufferen, når brokeren har kvitteret med PUBACK.
  Uden PUBACK inden 30 s sendes de igen; QuestDB fjerner eventuelle dubletter.
- Ingest gemmer DDATA med device-id'et, så målingerne står som `esp32-modbus`
  i QuestDB og på websitet, hvor de har deres eget gennemsnit.

MQTT-brugeren `esp32_gateway` må kun skrive til de fire emner ovenfor og læse
Thingy'ens NDATA (se [`cloud/mqtt/README.md`](../../cloud/mqtt/README.md)).
Brokerens certifikat kontrolleres mod ESP32's indbyggede CA-bundt.

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
| `include/credentials.h.example` | Skabelon til WiFi- og MQTT-koder |
| `proto/sparkplug_b.proto` | SparkplugB-delmængde; nanopb genererer koden ved build |
| `src/common/wifi_connect.cpp/.h` | Forbind som station og start access point |
| `src/slave/main.cpp` | Måleløkke og Modbus-server (eModbus) |
| `src/slave/temperature_sensor.cpp/.h` | DS18B20 uden blokering under målingen |
| `src/master/main.cpp` | Modbus-client, SparkplugB-session og hvornår der sendes |
| `src/master/reading_buffer.cpp/.h` | Buffer til målinger, til de er kvitteret |
| `src/master/sparkplug.cpp/.h` | Topics og protobuf-payloads (NBIRTH, NDEATH, DBIRTH, DDATA) |
| `src/master/mqtt_link.cpp/.h` | MQTT/TLS med ESP-IDF's esp-mqtt og last will |

eModbus og esp-mqtt kører i hver deres FreeRTOS-task. Slavens registre og
masterens buffer deles derfor med `loop()` og er beskyttet med en spinlock, og
MQTT-hændelser når `loop()` gennem en FreeRTOS-kø.

## Build og flash

Kræver [PlatformIO](https://platformio.org/). Kopiér først skabelonen og udfyld
hotspottets navn og kode, en kode til `aitsm-modbus` (mindst 8 tegn) og
MQTT-passwordet til `esp32_gateway`. `credentials.h` er i `.gitignore`.

```bash
cd Simple/modbus
cp include/credentials.h.example include/credentials.h
pio run                                                   # bygger begge
pio run -e master -t upload --upload-port /dev/ttyUSB0
pio run -e slave -t upload --upload-port /dev/ttyUSB1
pio device monitor -p /dev/ttyUSB0                        # masterens log
```

Portnumrene kan bytte plads, når boardene sættes i igen. Find boardet med
`esptool.py --port /dev/ttyUSB0 read_mac`, eller se efter `DS18B20 fundet` i
slavens log.

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

SparkplugB til VPS'en, samme dag med Thingy'en kørende:

```
10:26:27 [master] MQTT forbundet; NBIRTH og DBIRTH sendt (bdSeq 0)
10:31:36 [master] Batch fra Thingy:91 X set; sender egne målinger
10:31:36 [master] DDATA sendt: 60 målinger, 1869 bytes, seq 3
10:31:37 [master] DDATA bekræftet; 60 målinger fjernet fra bufferen
```

En DDATA blev fanget på brokeren med `cloud_ingest`s læseadgang og kørt
gennem ingest: 60 rækker med `device_id=esp32-modbus` og ét tidsstempel pr.
måling.

## Videre arbejde

- `DDEATH`, når slaven ikke svarer på Modbus, og `DBIRTH`, når den kommer igen.
- Kommandoer fra skyen (NCMD/DCMD), fx at ændre måleintervallet.
