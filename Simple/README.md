# AITSM Simple

Simple er den forenklede afleveringsversion af firmwaren til Nordic Thingy:91 X.
Den indeholder kun det, der skal til for at opfylde kravene til projekt C i
[kravspecifikationen](../docs/references/requirements/ucl-mt-project-requirements-2026-08-26.pdf)
(26-08-2026). Hver fil skal kunne forklares og forsvares af gruppen.

Simple bruger den fælles cloud i [`cloud/`](../cloud/) (MQTT-broker, ingest og
QuestDB) og samme SparkplugB-format som [Original](../Original/README.md). Der
er ingen separat cloud til Simple.

> **Status: udkast til gennemgang (#92).** Kravtabellen og modulforslaget
> herunder skal godkendes af gruppen, før der skrives kode i `app/` og `tests/`.

## Mappens indhold

- `app/` – Zephyr-applikationen (oprettes efter gennemgangen).
- `tests/` – målrettede firmware-tests (oprettes efter gennemgangen).
- `scripts/activate-ncs.sh` – aktiverer NCS v3.4.0-værktøjerne.

Kommandoer køres fra `Simple/`:

```bash
cd Simple
source scripts/activate-ncs.sh
west build -b thingy91x/nrf9151/ns -d build/thingy91x_nrf9151 app
```

## Foreslåede moduler

| Fil | Ansvar |
| --- | --- |
| `src/main.c` | Initialiserer modulerne og starter trådene. |
| `src/network.c` / `network.h` | `nrf_modem_lib` og `lte_lc`: NB-IoT, PSM og logning af netværksstatus. |
| `src/mqtt_client.c` / `mqtt_client.h` | TLS-forbindelse til egen broker via `mqtt_helper`, provisionering af CA-certifikat og publish med QoS 1. |
| `src/sensor.c` / `sensor.h` | Aflæser modemtemperatur, batterispænding og UTC-tid. |
| `src/batch.c` / `batch.h` | Fast buffer med målinger, der sendes samlet. |
| `src/sparkplug.c` / `sparkplug.h` + `proto/` | SparkplugB v1.0-topics og protobuf-payloads (NBIRTH og NDATA) med nanopb. |
| `prj.conf` / `Kconfig` | Måleinterval, batchstørrelse og de nødvendige NCS-subsystemer. |

Modbus (#93) kommer bagefter som `src/modbus.c` / `modbus.h`.

### Tråde og synkronisering

- **Måletråd:** aflæser sensoren hvert 15. sekund og lægger målingen i en
  `k_msgq`. Den sover i `k_sleep()` mellem målingerne.
- **Applikationstråd:** tager målinger fra køen, samler dem i batchen og
  publicerer NDATA, når batchen er fuld og MQTT er forbundet.
- **Netværk/MQTT:** callbacks fra `lte_lc` og `mqtt_helper` laver kun let
  arbejde og signalerer applikationstråden med `k_sem`/`k_event`.
- Batchbufferen bruges kun fra applikationstråden; deles den, beskyttes den med
  en `k_mutex`.

## Kravtabel: krav → fil → test/måling

| # | Krav (projekt C) | Fil(er) i Simple | Test eller måling |
| --- | --- | --- | --- |
| K1 | Zephyr 4.4.0 via NCS v3.4.0 med sysbuild | `scripts/activate-ncs.sh`, `app/CMakeLists.txt` | Firmware-build i `thingy91x/nrf9151/ns` |
| K2 | Kconfig via `prj.conf`; devicetree-afvigelser dokumenteret | `app/prj.conf`, `app/Kconfig` | Build; README dokumenterer, at der ikke er overlays |
| K3 | Modulær opdeling i `.c`/`.h` | Alle moduler ovenfor | Kodegennemgang |
| K4 | Multithreaded med kernel-synkronisering | `main.c` (måle- og applikationstråd, `k_msgq`, `k_sem`) | Serial log viser måle- og afsendelsestråd |
| K5 | Logging af netværksstatus, cloud-forbindelse, fejl og custom logs | `network.c`, `mqtt_client.c`, alle moduler | Serial log fra Thingy'en |
| K6 | Cellulær forbindelse via udleveret SIM (`nrf_modem`/`lte_lc`) | `network.c` | Serial log: LTE registreret |
| K7 | TLS | `mqtt_client.c`, `app/certs/` | Forbindelse til broker på port 8883 |
| K8 | MQTT til egen cloud | `mqtt_client.c` | Integrationstest: rækker i QuestDB |
| K9 | SparkplugB v1.0 (NBIRTH + NDATA) | `sparkplug.c`, `proto/` | Unit test `tests/sparkplug`; ingest afkoder payloaden |
| K10 | Data: device-id, timestamp og aflæste værdier | `sensor.c`, `sparkplug.c` | Unit test `tests/sparkplug`; QuestDB-rækker |
| K11 | Fast måleinterval på 5-15 s | `app/Kconfig`, måletråd | Integrationstest: interval mellem rækker er 15 s |
| K12 | Lavt dataforbrug via batching | `batch.c`, `app/Kconfig` | Unit test `tests/batch` |
| K13 | Lavt strømforbrug via sleep modes | `network.c` (PSM), måletråd (`k_sleep`) | Serial log: PSM tildelt; strømmåling med PPK2 (#99) |
| K14 | Egen cloud server til dataopsamling | Fælles [`cloud/`](../cloud/) | Integrationstest: Thingy → broker → QuestDB |
| K15 | Redegørelse for CRA og/eller IEC 62443 | Fælles [`docs/standarder-iec62443-cra.md`](../docs/standarder-iec62443-cra.md) | Dokumentation |
| K16 | Modbus RTU/TCP-dataopsamling | `modbus.c` (#93) | Fastlægges i #93 |

### Tests

- `tests/sparkplug`: koder NBIRTH og NDATA og afkoder dem igen med nanopb
  (topic, `seq`, timestamp og metrics).
- `tests/batch`: tilføj, fuld buffer og tømning efter vellykket publish.
- Integrationstest: Simple kører på Thingy'en mod den fælles broker, og
  QuestDB-tabellen `sensor_readings` kontrolleres for antal rækker, interval og
  dubletter i et fast tidsvindue.

## Taget med og bevidst udeladt

Listen føres løbende og bruges i rapporten.

| Fra Original | I Simple | Begrundelse |
| --- | --- | --- |
| LTE/NB-IoT, PSM | Med | Krav K6 og K13. |
| MQTT over TLS, CA-provisionering | Med | Krav K7 og K8. |
| SparkplugB NBIRTH/NDATA med nanopb | Med | Krav K9. |
| Batching med fast buffer | Med | Krav K12. |
| Single-afsendelse som alternativ profil | Udeladt | Kun ét krav om lavt dataforbrug; batching dækker det. |
| RGB-LED-status (`led_status.c`) | Udeladt | Ikke et krav; status ses i loggen. |
| Dedikeret MQTT-workqueue og afsendelsestokens | Udeladt | Robusthed ud over kravene; Simple har én applikationstråd. |
| PUBACK-timeout og genafsendelse med DUP-flag (#87) | Udeladt | Robusthed ud over kravene; QoS 1 og deduplikering i cloud er tilstrækkeligt. |
| Eksponentiel reconnect-backoff (#76) | Forenklet | Fast ventetid før nyt forsøg. |
| Måleværktøjer og profiler i `scripts/` | Udeladt | Bruges til målinger i Original; resultaterne gælder begge versioner. |
| AT-shell-overlay | Udeladt | Kun til test og målinger. |

## Til gennemgang i gruppen

1. Er modulopdelingen og trådmodellen god nok til at forklare til eksamen?
2. Skal batchen kun slettes efter PUBACK, eller er det nok at sende den med
   QoS 1 og stole på broker og cloud?
3. Skal reconnect være fast ventetid eller en simpel backoff?
4. Skal CA-provisioneringen ligge i `mqtt_client.c` eller i et separat modul?
