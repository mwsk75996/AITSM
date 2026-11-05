# SparkplugB v1.0

Kravspecifikationen kræver "SparkplugBv1.0 struktur som standard" for
MQTT-beskederne. Dette dokument forklarer, hvad SparkplugB er, hvordan vi bruger
det i Simple, og hvad det giver i forhold til det JSON-format, vi havde før
(#66).

## Hvad er SparkplugB?

MQTT bestemmer kun, *hvordan* beskeder flyttes, ikke hvad de indeholder eller
hvad topics hedder. SparkplugB er en åben standard fra Eclipse Foundation, der
lægger tre ting oven på MQTT:

| Del | Hvad den fastlægger |
| --- | --- |
| Topic-navne | `spBv1.0/<group_id>/<message_type>/<edge_node_id>` |
| Payload | Protobuf (binært) med en liste af *metrics*: navn, datatype, tidsstempel og værdi |
| Sessioner | En enhed melder sig med en *birth*-besked og nummererer derefter sine beskeder (`seq`) |

Standarden er lavet til industrielle systemer (SCADA og PLC'er), hvor mange
enheder fra forskellige leverandører skal tale med de samme systemer uden en
aftale for hver enhed.

## Sådan bruger vi det

Thingy:91 X er en *edge node* i gruppen `aitsm`. Der er ingen enheder under den.

```
spBv1.0/aitsm/NBIRTH/thingy91x    ved hver ny MQTT-forbindelse
spBv1.0/aitsm/NDATA/thingy91x     en batch med målinger
```

Gruppe og node-id sættes i Kconfig (`CONFIG_AITSM_SPARKPLUG_GROUP_ID` og
`CONFIG_AITSM_SPARKPLUG_EDGE_NODE_ID`).

### Forløbet

```
Thingy                                   Broker
  │── CONNECT (TLS, 8883) ────────────────▶│
  │── NBIRTH  seq 0, bdSeq n ─────────────▶│
  │◀─────────────────────────────── PUBACK │   først nu må der sendes data
  │── NDATA   seq 1, 20 målinger ─────────▶│
  │◀─────────────────────────────── PUBACK │   batchen fjernes fra bufferen
  │── NDATA   seq 2 ... ──────────────────▶│
  ✕  forbindelsen falder: bdSeq n+1, ny NBIRTH ved næste forbindelse
```

- **NBIRTH** sendes ved hver forbindelse og skal være kvitteret, før første
  NDATA. Den har `seq` 0 og metric'en `bdSeq`, der tæller forbindelser siden
  opstart. Den erklærer også de metrics, der kommer i NDATA (`temperature` og
  `battery`), uden værdier.
- **NDATA** har `seq` 1, 2, … og starter forfra efter 255. Hver måling giver to
  metrics af typen Float: `temperature` i °C og `battery` i %. Begge har
  målingens eget tidsstempel i millisekunder (UTC) og er markeret
  `is_historical`, fordi de er målt før afsendelsen.
- **`seq` tælles først op ved PUBACK.** Bliver en NDATA sendt igen, fordi
  kvitteringen ikke kom, har den samme `seq` som før. Batchen bliver i bufferen,
  til PUBACK er modtaget.

Alt sendes med QoS 1 over TLS. Koden ligger i
[`Simple/app/src/sparkplug.c`](../Simple/app/src/sparkplug.c), og forløbet ligger i
[`Simple/app/src/main.c`](../Simple/app/src/main.c).

### Protobuf på enheden

Payloaden kodes med nanopb, der er med i NCS. Vi bruger en delmængde af den
officielle `sparkplug_b.proto` med samme feltnumre og datatyper, så alle
SparkplugB-klienter kan læse beskederne. DataSet, Template, MetaData og
PropertySet er udeladt, fordi vi ikke bruger dem. Metric-listen kodes med et
callback direkte fra batchbufferen, så der ikke skal en ekstra kopi i RAM. Se
[`Simple/app/proto/README.md`](../Simple/app/proto/README.md).

### I skyen

`ingest.py` abonnerer på `spBv1.0/+/NBIRTH/+` og `spBv1.0/+/NDATA/+`. Den
afkoder protobuf med en lille indbygget afkoder, så der ikke kræves ekstra
Python-pakker. Edge node-id'et fra topicet bliver til `device_id`, og hver
måling bliver én række i QuestDB med målingens eget tidsstempel. NBIRTH
kvitteres og logges, men gemmes ikke. Se
[`cloud/mqtt/README.md`](../cloud/mqtt/README.md).

## Hvad det forbedrer

Før #66 sendte firmwaren sit eget JSON-format til `aitsm/thingy91x/telemetry`:

```json
{"device_id":"thingy91x","readings":[{"timestamp":1762300800,"temperature":23.45,"battery":87.50}, ...]}
```

| | Eget JSON | SparkplugB |
| --- | --- | --- |
| Format | Vores egen aftale, som kun vores ingest forstår | Åben standard, som SCADA-, PLC- og IIoT-platforme kan læse direkte |
| Topics | Frit valgt | Fast struktur, så gruppe, beskedtype og enhed kan ses af topicet |
| Datatyper | Alt er tekst, og modtageren gætter typen | Hver metric har en erklæret datatype (Float, Int64, …) |
| Tidsstempler | Sekunder | Millisekunder pr. metric, og gamle målinger kan markeres `is_historical` |
| Ny forbindelse | Kan ikke ses i data | NBIRTH og `bdSeq` viser, at en ny session er startet |
| Rækkefølge og huller | Kan ikke ses | `seq` gør tabte eller dublerede beskeder synlige |
| Hvilke data findes | Skal stå i dokumentation | NBIRTH erklærer enhedens metrics |
| Kodning på enheden | `snprintf` og egen strengopbygning | Genereret protobuf-kode (nanopb) med faste grænser |

Den største gevinst er ikke færre bytes, men at data har en kendt struktur. En
anden IoT-platform eller et SCADA-system, der forstår SparkplugB, kan tage imod
data fra Thingy'en uden en speciallavet parser. Det er også det, der gør
"Nice to have"-kravet om kommandoer fra skyen (NCMD) muligt senere.

### Datamængde

SparkplugB sparer kun lidt data hos os. En NDATA med 20 målinger fylder 1169
bytes, målt i [dataforbrug.md](dataforbrug.md). Det svarer til ca. 58 bytes pr.
måling. Den samme batch i det gamle JSON-format ville fylde ca. 1260 bytes, så
SparkplugB er ca. 7 % mindre. Grunden er, at metric-navnene `temperature` og
`battery` står i hver eneste måling. Protobuf er kun meget mindre end JSON, når
navnene erstattes af *aliases* (se nedenfor).

Overhead fra TCP/IP, TLS og MQTT betyder mere end selve formatet. Det var
batching (#98), der skar dataforbruget til en fjerdedel, ikke formatet.

## Bevidst udeladt

| Del af standarden | Hvorfor den mangler |
| --- | --- |
| **NDEATH** som MQTT last will | NCS' `mqtt_helper` kan kun sætte et last will som fast Kconfig-tekst med QoS 0, og en protobuf-payload med `bdSeq` kan ikke skrives som tekst. Skyen ser derfor en afbrudt enhed som manglende data, ikke som en NDEATH. |
| **Aliases** | Ville spare ca. 18 bytes pr. måling, men kræver, at ingest husker koblingen mellem alias og navn fra NBIRTH. Det er ikke det værd ved vores datamængde. |
| **NCMD** (kommandoer fra skyen) | Kun "Nice to have" i kravspecifikationen. Enheden abonnerer ikke på noget i dag. |
| **DBIRTH/DDATA** | Der er ingen enheder under Thingy'en. |
| **STATE fra en primær host** | Bruges til at skifte mellem flere SCADA-værter. Vi har kun ingest. |
| **Kontrol af `seq` i ingest** | `seq` logges, men ingest reagerer ikke på huller. Dubletter fanges i stedet af QuestDB's deduplikering på `device_id` og tidsstempel (#78). |

Vi følger altså SparkplugB's topic-struktur, payload-format og birth/seq-forløb
fuldt ud for NBIRTH og NDATA. Den del af standarden, der handler om at
opdage en død enhed, er ikke implementeret.

## Test

- **Unit tests** i [`Simple/tests/sparkplug`](../Simple/tests/sparkplug) (ni
  testcases, kører i CI på `native_sim`): topic-format, `seq` der starter forfra
  efter 255, NBIRTH med `seq` 0, `bdSeq` og erklærede metrics, NDATA med værdi,
  tidsstempel og `seq`, en fuld batch med `is_historical`, for lille buffer og
  at størrelsesmakroerne rummer en fuld batch. Testene afkoder payloaden igen og
  kontrollerer felterne.
- **Ingest** har pytest-tests af protobuf-afkoderen og af SparkplugB-vejen til
  QuestDB.
- **På boardet** kører firmwaren i drift mod VPS'en: NBIRTH ved hver
  forbindelse, derefter NDATA, og QuestDB får én række pr. måling (se
  [`Simple/README.md`](../Simple/README.md#verifikation)).

## Videre arbejde

1. NDEATH via egen MQTT-forbindelse uden `mqtt_helper` eller en
   protobuf-kodet last will, så skyen kan se, at enheden er gået ned.
2. Aliases, hvis datamængden skal ned.
3. NCMD, fx `Node Control/Rebirth` eller ændring af måleintervallet fra skyen.
