# AITSM Simple

Simple er den forenklede afleveringsversion af firmwaren til Nordic Thingy:91 X.
Den indeholder kun det, der skal til for at opfylde kravene til projekt C i
[kravspecifikationen](../docs/references/requirements/ucl-mt-project-requirements-2026-08-26.pdf)
(26-08-2026). Hver fil skal kunne forklares og forsvares af gruppen.

Simple bruger den fælles cloud i [`cloud/`](../cloud/) (MQTT-broker, ingest og
QuestDB) og samme SparkplugB-format som [Original](../Original/README.md). Der
er ingen separat cloud til Simple.

> **Status (#92):** Modulerne og kravtabellen er godkendt af gruppen i
> [PR #100](https://github.com/mwsk75996/AITSM/pull/100). Firmwaren bygger,
> unit-testene består, og første integrationstest på Thingy'en er gennemført
> (se [Verifikation](#verifikation)). Reconnect efter udfald er ikke testet på
> hardware endnu.

## Mappens indhold

- `app/` – Zephyr-applikationen: kildekode i `src/`, SparkplugB-protobuf i
  `proto/` og brokerens CA-certifikat i `certs/`.
- `tests/` – unit tests til `batch.c` og `sparkplug.c`.
- `scripts/activate-ncs.sh` – aktiverer NCS v3.4.0-værktøjerne.

## Build, flash og test

Kommandoer køres fra `Simple/`. MQTT-passwordet gives som miljøvariabel ved
build og gemmes ikke i repositoriet:

```bash
cd Simple
source scripts/activate-ncs.sh
export AITSM_MQTT_PASSWORD='...'
west build -b thingy91x/nrf9151/ns -d build/thingy91x_nrf9151 app
```

Flash sker som i [Original](../Original/README.md#flash-til-thingy91-x) med
`build/thingy91x_nrf9151/dfu_application.zip`. Unit-testene kører uden
hardware:

```bash
west twister -p native_sim/native/64 -T tests/batch -T tests/sparkplug
```

Der bruges ingen devicetree-overlays; boardets standard-devicetree for
`thingy91x/nrf9151/ns` bruges uændret.

## Moduler

| Fil | Ansvar |
| --- | --- |
| `src/main.c` | Initialiserer modulerne og indeholder de to tråde: måletråden og applikationstråden. |
| `src/network.c` / `network.h` | `nrf_modem_lib` og `lte_lc`: NB-IoT, PSM og logning af netværksstatus. |
| `src/mqtt_client.c` / `mqtt_client.h` | TLS-forbindelse til egen broker via `mqtt_helper`, provisionering af CA-certifikat og publish med QoS 1. |
| `src/sensor.c` / `sensor.h` | Aflæser modemtemperatur, batterispænding og UTC-tid. Venter med et `k_sem` på, at netværket har leveret UTC-tid. |
| `src/batch.c` / `batch.h` | Fast buffer med målinger, der sendes samlet. |
| `src/sparkplug.c` / `sparkplug.h` + `proto/` | SparkplugB v1.0-topics og protobuf-payloads (NBIRTH og NDATA) med nanopb. |
| `prj.conf` / `Kconfig` | Måleinterval, batchstørrelse, bufferstørrelse, broker og de nødvendige NCS-subsystemer. |

Modbus (#93) kommer bagefter. Selve Modbus-koden ligger på ESP32'en. På Thingy'en publicerer `src/modbus.c` / `modbus.h` en simpel MQTT-besked til den fælles broker; ESP32'en abonnerer på den via VPS'en og udfører Modbus RTU-skrivningen. **Foreløbigt:** løsningen skal godkendes af underviseren, før den vælges endeligt, fordi kravet nævner Zephyrs Modbus-API og dataopsamling; se #93.

### Tråde og synkronisering

- **Måletråd:** venter på UTC-tid (`k_sem`), aflæser derefter sensoren hvert
  15. sekund og lægger målingen i en `k_msgq`. Den sover i `k_sleep()` mellem
  målingerne.
- **Applikationstråd** (`main`): sover i `k_event_wait_safe()`, til der sker
  noget. Den tager målinger fra køen, samler dem i batchen, forbinder til
  brokeren og publicerer NBIRTH og NDATA.
- **Netværk/MQTT:** callbacks fra `lte_lc` og `mqtt_helper` kører i bibliotekernes
  egne tråde. De gemmer kun en status og vækker applikationstråden med
  `k_event_post()`.
- Batchbufferen bruges kun fra applikationstråden og har derfor ingen lås.

### Forløb

1. `main` initialiserer modemmet, gemmer CA-certifikatet i modemmet og starter
   NB-IoT-forbindelsen.
2. Når enheden er på nettet, forbinder applikationstråden til brokeren over TLS
   og sender NBIRTH. Måletråden starter, når UTC-tiden er modtaget.
3. Hver måling lægges i batchen. Ved 20 målinger (`CONFIG_AITSM_BATCH_SIZE`)
   sendes de samlet som én NDATA med QoS 1.
4. Batchen bliver i bufferen, til brokeren har svaret med PUBACK. Bufferen har
   plads til 28 målinger (`CONFIG_AITSM_BATCH_CAPACITY`), så nye målinger kan
   gemmes imens.
5. Ryger forbindelsen, fortsætter målingerne. Applikationstråden prøver igen
   efter 5, 10, 20, 40 og derefter højst 60 s, og sender NBIRTH og de gemte
   målinger, når forbindelsen er tilbage.

## Kravtabel: krav → fil → test/måling

| # | Krav (projekt C) | Fil(er) i Simple | Test eller måling |
| --- | --- | --- | --- |
| K1 | Zephyr 4.4.0 via NCS v3.4.0 med sysbuild | `scripts/activate-ncs.sh`, `app/CMakeLists.txt` | Firmware-build i `thingy91x/nrf9151/ns` |
| K2 | Kconfig via `prj.conf`; devicetree-afvigelser dokumenteret | `app/prj.conf`, `app/Kconfig` | Build; README dokumenterer, at der ikke er overlays |
| K3 | Modulær opdeling i `.c`/`.h` | Alle moduler ovenfor | Kodegennemgang |
| K4 | Multithreaded med kernel-synkronisering | `main.c` (måle- og applikationstråd, `k_msgq`, `k_event`), `sensor.c` (`k_sem`) | Serial log viser måle- og afsendelsestråd |
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
| K16 | Modbus RTU/TCP-dataopsamling | `modbus.c` (MQTT-besked fra Thingy'en); Modbus RTU-master på ESP32 (#93) | Fastlægges i #93 |

### Tests

- `tests/sparkplug`: koder NBIRTH og NDATA og afkoder dem igen med nanopb
  (topic, `seq`, timestamp og metrics), og kontrollerer, at en fuld buffer kan
  være i én besked.
- `tests/batch`: tilføj, klar ved batchstørrelsen, fuld buffer uden
  overskrivning og fjernelse efter PUBACK, hvor målinger tilføjet imens bevares.
- Integrationstest: Simple kører på Thingy'en mod den fælles broker, og
  QuestDB-tabellen `sensor_readings` kontrolleres for antal rækker, interval og
  dubletter i et fast tidsvindue.

### Verifikation

Kørt 12-10-2026 på Thingy:91 X mod den fælles broker:

- Serial log: registreret på NB-IoT, PSM tildelt (TAU 4200 s, aktiv tid 10 s),
  forbundet over TLS efter 16 s, NBIRTH bekræftet, måling hvert 15. sekund.
- Første NDATA: 20 målinger, 1169 bytes, `seq` 1, PUBACK efter 6 s. Derefter
  blev de 20 målinger fjernet fra bufferen.
- Ingest: `Sparkplug B NDATA from thingy91x seq=1 with 20 reading(s)` og
  `Stored 20 telemetry row(s)`.
- QuestDB: 20 rækker fra 07:41:26 til 07:46:10 UTC med 15 s mellem rækkerne
  (14 s mellem de to første, hvor uret lige var sat) og ingen dubletter.

## Taget med og bevidst udeladt

Listen føres løbende og bruges i rapporten.

| Fra Original | I Simple | Begrundelse |
| --- | --- | --- |
| LTE/NB-IoT, PSM | Med | Krav K6 og K13. |
| MQTT over TLS, CA-provisionering | Med | Krav K7 og K8. |
| SparkplugB NBIRTH/NDATA med nanopb | Med | Krav K9. |
| Batching med fast buffer | Med | Krav K12. Batchen slettes først efter PUBACK (kvittering), så målinger ikke tabes ved afbrud. |
| Målinger under udfald (#77) | Med | Målingerne fortsætter hvert 15. s og bliver i bufferen, til forbindelsen er tilbage. Er bufferen fuld, springes nye målinger over og tælles i loggen; intet overskrives. |
| Single-afsendelse som alternativ profil | Udeladt | Kun ét krav om lavt dataforbrug; batching dækker det. |
| RGB-LED-status (`led_status.c`) | Udeladt | Ikke et krav; status ses i loggen. |
| Dedikeret MQTT-workqueue og afsendelsestokens | Udeladt | Robusthed ud over kravene; Simple har én applikationstråd. |
| PUBACK-timeout og genafsendelse med DUP-flag (#87) | Udeladt | Robusthed ud over kravene; QoS 1 og deduplikering i cloud er tilstrækkeligt. |
| Eksponentiel reconnect-backoff (#76) | Med, forenklet | Backoff 5, 10, 20, 40 og højst 60 s, nulstilles ved forbindelse; uden Originals ekstra tilstandshåndtering. |
| Måleværktøjer og profiler i `scripts/` | Udeladt | Bruges til målinger i Original; resultaterne gælder begge versioner. |
| AT-shell-overlay | Udeladt | Kun til test og målinger. |
| Batch-afsendelse efter tid (300 s) | Udeladt | Simple sender, når der er 20 målinger. Det svarer til 5 minutter ved 15 s interval. |
| MQTT-disconnect ved LTE-tab | Udeladt | Simple venter på, at MQTT-forbindelsen selv melder fejl, og forbinder derefter igen med backoff. |
| Eventkø og reconnect-tilstandsmaskine (`app_controller.c`, `mqtt_reconnect.c`) | Udeladt | Erstattet af én løkke i `main.c` med `k_event`. |
| Valgfrit log-niveau (`CONFIG_AITSM_LOG_LEVEL`) | Udeladt | Alle moduler logger på info-niveau. |

## Til gennemgang i gruppen

1. Er modulopdelingen og trådmodellen god nok til at forklare til eksamen?
2. Skal batchen kun slettes efter PUBACK (kvittering), eller er det nok at sende den med
   QoS 1 og stole på broker og cloud?
3. Hvordan skal enheden forbinde igen, når MQTT-forbindelsen til brokeren
   ryger (reconnect)?
   - **Fast ventetid:** prøv igen efter fx 10 s, hver gang. Det er nemmest at
     forklare, men hvis brokeren er nede længe, bruger enheden strøm og data
     på mange forgæves forsøg.
   - **Simpel backoff:** ventetiden fordobles efter hvert mislykket forsøg
     (fx 5, 10, 20, 40 s) op til et loft på 60 s og nulstilles, når
     forbindelsen lykkes. Det giver færre forsøg under længere udfald og er
     det, Original gør (#76), men det er lidt mere kode.
4. Hvor skal CA-provisioneringen ligge?

   **CA-provisionering:** TLS-forbindelsen til brokeren kræver, at enheden kan
   kontrollere brokerens certifikat. Det gør den med CA-certifikatet (Let's
   Encrypts rodcertifikat i `app/certs/`). Ved opstart skriver firmwaren
   certifikatet ind i modemmets sikre nøglelager under et `sec_tag` med
   `modem_key_mgmt_write()`, og TLS-forbindelsen bruger det derefter. I
   Original ligger det i `mqtt_credentials_provision.c`.

   Skal det i Simple ligge i `mqtt_client.c` (færre filer, fordi kun MQTT
   bruger det) eller i et separat modul, fx `tls_credentials.c` (tydeligere
   ansvar, og det kan genbruges, hvis andet end MQTT får brug for TLS)?

### Beslutninger

Godkendt i [PR #100](https://github.com/mwsk75996/AITSM/pull/100):

1. Modulopdelingen og trådmodellen ovenfor bruges.
2. Batchen slettes først efter PUBACK (kvittering).
3. Reconnect bruger simpel backoff (5, 10, 20, 40 og højst 60 s).
4. CA-provisioneringen ligger i `mqtt_client.c`.
