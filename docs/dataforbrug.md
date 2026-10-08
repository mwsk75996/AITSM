# Dataforbrug og leveringstid for transmissionsprofiler (#32)

Dette dokument viser, hvor mange data de fire transmissionsprofiler bruger, og hvor
lang tid der går fra måling til bekræftet afsendelse. Strømforbruget er **ikke**
målt endnu (kræver PPK2) og tilføjes, når måleudstyret er på plads.

## Testbetingelser

| | |
| --- | --- |
| Dato | 2026-10-08 |
| Enhed | Thingy:91 X (nRF9151), NB-IoT, roaming-net, stationær på indendørs bord |
| Firmware | `main` efter PSM (#23) og SparkplugB (#66), bygget med `overlay-at-shell.conf` og profilens `.conf` fra `scripts/measurement-profiles/` |
| Måleinterval | 15 s i alle profiler |
| PSM | TAU 4200 s, aktiv tid 10 s (tildelt af nettet) |
| MQTT | TLS, QoS 1, keepalive 1200 s, SparkplugB NBIRTH + NDATA |
| Dataopsamling | modemmets `AT%XCONNSTAT`, aktiveret før TLS-handshaket |
| Metode | `scripts/measure-data-usage.py --profile <p> --minutes <n>` |

Tællerne tæller IP-data (inkl. TCP/IP, TLS og MQTT) i **hele kilobyte**, så
tallene pr. time er et estimat med en usikkerhed på nogle få KB. Måleperioden
starter efter bekræftet NBIRTH, så forbindelsesopbygningen er målt for sig.

## Profiler

| Profil | Konfiguration | Afsendelse |
| --- | --- | --- |
| `single` | `AITSM_TRANSMISSION_SINGLE` | hver måling (15 s) |
| `batch1` | batch 60 s, 4 målinger | hvert minut |
| `batch5` | batch 300 s, 20 målinger (standard) | hver 5. minut |
| `batch8` | batch 900 s, 32 målinger, payload 4096 bytes | hver 8. minut (bufferen er fuld) |

En 15-minutters batch kan ikke bygges: den kræver 60 målinger, og 60 × ca. 70
bytes rummes ikke i den højest tilladte payload på 4096 bytes. `batch8` er den
længste mulige batch.

## Resultater

| Profil | Periode | Beskeder | Bytes pr. besked (payload) | Payload pr. time | Data pr. time (IP) | Gns. / maks. leveringstid | Retries / reconnects / fejl |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `single` | 20 min | 79 | 63 | ca. 15,1 KB | ca. 66 KB | 2,1 s / 24 s | 0 / 0 / 0 |
| `batch1` | 20 min | 20 | 241 | ca. 14,5 KB | ca. 33 KB | 25 s / 49 s | 0 / 0 / 0 |
| `batch5` | 30 min | 5 | 1169 | ca. 14,0 KB | ca. 16 KB | 146 s / 289 s | 0 / 0 / 0 |
| `batch8` | 33 min | 4 | 1865 | ca. 14,0 KB | ca. 16 KB | 235 s / 468 s | 0 / 0 / 0 |

Forbindelsesopbygning (TLS-handshake, første NBIRTH og eventuel tømning af
bufferen): 4 til 8 KB modtaget og 1 KB sendt, én gang pr. forbindelse.

### Det viser tallene

- Selve nyttelasten er næsten den samme i alle profiler (ca. 58 bytes pr. måling,
  14 til 15 KB i timen). Forskellen er **overhead**: TCP/IP-headere, TLS-records,
  MQTT-headere og kvitteringer pr. besked. Single bruger ca. 4,4 gange payloaden,
  batch 5 minutter ca. 1,1 gange.
- Batch hvert 5. minut bruger en fjerdedel af single's datamængde og halvdelen af
  batch 1 minut.
- `batch8` sparer ikke mere end `batch5` inden for målingens opløsning (16 KB i
  timen), men forlænger ventetiden fra gennemsnitligt 2,4 til 3,9 minutter.
- Modemmet er RRC-idle størstedelen af tiden i batch-profilerne (se #23). Single
  vækker modemmet 4 gange i minuttet, så modemmet kan næppe nå at sove i PSM.
  Det forventes at give det største strømforbrug; det skal bekræftes med PPK2.

## Anbefalet standardprofil

**Batch hvert 5. minut** (nuværende standard). Den bruger ca. 16 KB i timen
(ca. 0,4 MB i døgnet), leverer en måling senest efter 5 minutter, og modemmet
kan sove imellem. `batch8` giver ingen målbar gevinst, og `single` bruger
fire gange så meget data.

## Observation: ingen margin i bufferen ved batch 5 minutter

Standarden `AITSM_BATCH_MAX_SAMPLES=20` svarer præcis til en batch (20 × 15 s).
Ved den sjette batch i `batch5` kom PUBACK ikke inden for 15 sekunder. Bufferen
var fuld, og målingen 15 sekunder senere blev sprunget over (`Målebuffer fuld`).
De første fem batches gik uden tab. En forsinket kvittering kan altså koste en
måling. En større buffer (fx 24 målinger) giver plads til op til ca. to minutters
forsinkelse uden at ændre sendetidspunktet (batchen sendes stadig efter 300 s).

## Begrænsninger

- Opløsning på 1 KB og korte måleperioder (20 til 33 minutter) giver et estimat,
  ikke en præcis værdi. Signalforhold og operatørens NAT kan ændre resultaterne.
- Én enhed og ét net. LTE-M er ikke målt, fordi enheden er konfigureret til
  NB-IoT (#25).
- Strøm, hvilket kræver PPK2, er ikke målt og mangler for at lukke #32.

## Gentag målingen

```bash
source scripts/activate-ncs.sh
west build -p -b thingy91x/nrf9151/ns -d build/measure-batch5 app -- \
  "-DEXTRA_CONF_FILE=overlay-at-shell.conf;$PWD/scripts/measurement-profiles/batch5.conf"
nrfutil device program --firmware build/measure-batch5/dfu_application.zip \
  --serial-number <id> --traits mcuBoot --family nrf91 \
  --options target=nRF91,mcu_end_state=NRFDL_MCU_STATE_APPLICATION
python3 scripts/measure-data-usage.py --profile batch5 --minutes 30
```
