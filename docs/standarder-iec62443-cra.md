# Standarder: IEC 62443-4-2 og CRA

Denne redegørelse dækker issue #11 og #106. Formålet er ikke fuld compliance,
men at udvælge relevante krav fra IEC 62443-4-2 og CRA Annex I og argumentere
for, hvordan løsningen adresserer dem, og hvor grænserne går.

Kilder:

- IEC 62443-4-2: ANSI/ISA-62443-4-2-2018,
  [`references/standards/iec-62443-4-2.pdf`](references/standards/iec-62443-4-2.pdf).
  Afsnitsnumrene herunder henviser til standarden.
- CRA Annex I: de udvalgte paragraffer i
  [kravspecifikationen](references/requirements/ucl-mt-project-requirements-2026-08-26.pdf),
  som citerer Kommissionens forslag (15.9.2022). Nummereringen herunder følger
  kravspecifikationen. I den vedtagne forordning har punkterne andre bogstaver.

## Afgrænsning

- Vurderingen gælder afleveringsversionen [`Simple/`](../Simple/README.md) og
  den fælles cloud i [`cloud/`](../cloud/). **Original er uden for scope.**
- Vurderingen dækker de seks udvalgte krav. Den er ikke et samlet opnået
  Security Level og ikke en certificering.
- Modbus (#93) er ikke med endnu. Når løsningen er afklaret, vurderes bl.a.
  CR 5.1 (netværkssegmentering) og CR 2.1 (autorisation), fordi Modbus hverken
  har login eller kryptering.

## Systemkomponenter og zoner

IEC 62443-4-2 opdeler komponenter i fire typer: embedded device (EDR), host
device (HDR), network device (NDR) og software application (SAR). CR-kravene
gælder alle typer. EDR-kravene gælder kun embedded devices.

| Komponent | Type | Zone | Rolle |
| --- | --- | --- | --- |
| Thingy:91 X med Simple-firmware | EDR | Felt | Måler og sender telemetri |
| VPS'en (Ubuntu med UFW, SSH og fail2ban) | HDR | Cloud | Værten for containerne og websitet |
| Mosquitto | SAR | Cloud | MQTT-broker, eneste tjeneste ud mod internettet for enheden |
| Ingest (`cloud/mqtt/ingest.py`) | SAR | Cloud | Afkoder SparkplugB og skriver til QuestDB |
| QuestDB | SAR | Cloud | Tidsseriedatabase |

```
 Zone: Felt                    Conduit                      Zone: Cloud
┌──────────────┐   MQTT over TLS :8883 via NB-IoT   ┌──────────────────────────────────┐
│ Thingy:91 X  │ ─────────────────────────────────▶ │ Mosquitto ─▶ ingest ─▶ QuestDB   │
│ (EDR)        │   ikke-betroet net (internet)      │ (SAR)      (SAR)       (SAR)     │
└──────────────┘                                    │            └─ internt netværk ─┘ │
                                                    └──────────────────────────────────┘
```

- Enheden åbner kun én udgående forbindelse og lytter ikke på nogen porte.
- Conduit'en er den eneste vej mellem zonerne. Brokeren er den eneste tjeneste,
  Docker udgiver på internettet.
- Ingest ligger kun på det interne Docker-netværk `backend` (`internal: true`)
  uden adgang udefra eller til internettet.
- QuestDB er udgivet på værtens `127.0.0.1:9000`, så fra værten kan den kun nås
  lokalt, fx af `api.php`. Den er desuden tilsluttet Docker-netværkene `edge` og
  `backend` og kan derfor nås af de andre containere på samme netværk via
  `questdb:9000`. Ingest skriver fx til `http://questdb:9000/write`, og
  Mosquitto, som også er på begge netværk, kan teknisk set nå den.

Hvor de seks krav er implementeret:

| Komponent | CR 1.2 | CR 2.8 | CR 3.1 | CR 3.7 | CR 4.1 | EDR 3.14 |
| --- | --- | --- | --- | --- | --- | --- |
| Thingy:91 X (EDR) | ✔ MQTT-login | ✔ Zephyr-log | ✔ TLS, verificerer brokeren | ✔ backoff, PUBACK-timeout | delvist: password i flash | ✔ NSIB og MCUboot |
| VPS (HDR) | ✔ SSH kun med nøgler | ✔ journal, fail2ban | – | – | ✔ credentials-fil kun for root | – |
| Mosquitto (SAR) | ✔ ingen anonyme, ACL | ✔ forbindelser, afviste logins | ✔ TLS ≥ 1.2 | – | ✔ 1883 kun internt | – |
| Ingest (SAR) | ✔ egen MQTT-bruger, kun læs | ✔ batches, afviste payloads | – | ✔ afviser ugyldige payloads, retry | – | – |
| QuestDB (SAR) | – (intet login; kun via værtens 127.0.0.1 og containerne på samme netværk) | – | – | – | delvist: ikke krypteret på disk | – |

QuestDB's manglende adskillelse af læse- og skriveadgang følges i #107.

## Security Level

Standarden skelner mellem:

- **SL-T** (target): det sikkerhedsniveau, systemet eller en zone skal nå.
- **SL-C** (capability): det niveau, en komponent kan levere for et krav.

### Valgt SL-T

- **SL 1 for systemet.** SL 1 beskytter mod tilfældige fejl og simple
  overtrædelser. Telemetrien (temperatur og batteri) er hverken følsom eller
  sikkerhedskritisk, og systemet styrer ikke noget fysisk.
- **SL 2 for conduit'en Thingy → broker.** Den går over et ikke-betroet net og
  er den eneste vej ind i Cloud-zonen. Derfor sikres den et niveau højere.
- **Uden for trusselsmodellen:** fysisk adgang til enheden. Det er en prototype
  på et udviklingskit, og kravspecifikationen udelukker selv avanceret PKI.

### SL-mapping for de seks krav

| Krav | SL 1 | SL 2 | SL 3–4 | Afsnit |
| --- | --- | --- | --- | --- |
| CR 1.2 | ikke valgt | grundkrav | RE(1) unik identifikation | 5.4.4 |
| CR 2.8 | grundkrav | grundkrav | grundkrav | 6.10.4 |
| CR 3.1 | grundkrav | RE(1) communication authentication | RE(1) | 7.3.4 |
| CR 3.7 | grundkrav | grundkrav | grundkrav | 7.9.4 |
| CR 4.1 | grundkrav | grundkrav | grundkrav | 8.3.4 |
| EDR 3.14 | grundkrav | RE(1) authenticity of the boot process | RE(1) | 13.9.4 |

### Vurderet SL-C

| Krav | Krævet til SL-T | Opnået SL-C | Gap |
| --- | --- | --- | --- |
| CR 1.2 | grundkrav (conduit SL 2) | SL 2 | Delt password og fast klient-id; RE(1) kræver unik identitet (#71) |
| CR 2.8 | grundkrav | delvist adresseret | Ingen hændelses-id eller kategorifelt; konfigurationsændringer og backup logges ikke; log på enheden er ikke persistent |
| CR 3.1 | RE(1) (conduit SL 2) | SL 2 | – |
| CR 3.7 | grundkrav | grundkrav adresseret | – |
| CR 4.1 | grundkrav | delvist | Password ukrypteret i flash, QuestDB ukrypteret på disk |
| EDR 3.14 | grundkrav (SL 1) | SL 1, testet på boardet | RE(1) kræver egen root of trust (#70) |

CR 2.8 og CR 3.7 har ingen requirement enhancements, så det samme grundkrav
gælder på alle fire niveauer. Det betyder ikke, at komponenterne opnår SL-C 4.
Vurderingen er kun, at de valgte tiltag adresserer grundkravet med de forbehold,
der står under hvert krav.

## Krav

### CR 1.2 – Software process and device identification and authentication

**Krav (5.4):** Komponenter skal kunne identificere sig og autentificere sig
over for andre komponenter. Kræves fra SL 2. RE(1) kræver unik identifikation
og autentificering fra SL 3.

| Komponent | Implementering | Bevis | Status |
| --- | --- | --- | --- |
| Thingy:91 X | Logger ind på brokeren med brugernavn og password | `aitsm_mqtt_connect()` i `Simple/app/src/mqtt_client.c`; firmwaren starter ikke uden password (`aitsm_mqtt_init()`) | Opfyldt |
| Mosquitto | Afviser anonyme klienter; ACL'en giver `thingy91x` skriveadgang til sine egne SparkplugB-topics og `cloud_ingest` kun læseadgang | `allow_anonymous false`, `password_file` og `acl_file` i `cloud/docker/mosquitto/mosquitto.conf` | Opfyldt |
| Ingest | Egen MQTT-bruger (`cloud_ingest`) med læseadgang | ACL på brokeren | Opfyldt |

**Afgrænsning:** Passwordet er fælles for enheden og ligger ukrypteret i
firmwaren (`static char password[]` i `mqtt_client.c`). Klient-id'et er fast
(`CONFIG_AITSM_SPARKPLUG_EDGE_NODE_ID`). Det er tilstrækkeligt med én enhed,
men ikke en unik identitet i RE(1)'s forstand. En mulig løsning er mTLS med en
nøgle, der er unik for hver enhed (#71).

### CR 2.8 – Auditable events

**Krav (6.10):** Komponenten skal kunne generere audit-records for kategorierne
adgangskontrol, fejl i forespørgsler, systemhændelser, backup og gendannelse,
konfigurationsændringer og hændelser i selve audit-loggen. Hver post skal have
tidsstempel, kilde, kategori, type, hændelses-id og resultat. Grundkrav på alle
niveauer.

| Komponent | Implementering | Bevis | Status |
| --- | --- | --- | --- |
| Thingy:91 X | Zephyrs logging-subsystem, ét logmodul pr. fil: netværksstatus og PSM, forbindelse, afvisning og lukning af MQTT, afsendelse, PUBACK, timeout og oversprungne målinger | `LOG_MODULE_REGISTER` i `network.c`, `mqtt_client.c` og `main.c` | Adresseret |
| Mosquitto | Logger forbindelser og afviste logins | `log_dest stdout`, `docker compose logs mosquitto` | Adresseret |
| Ingest | Logger modtagne batches med `seq`, afviste payloads og skrivefejl | `cloud/mqtt/ingest.py`, `docker compose logs ingest` | Adresseret |
| VPS | SSH-login i journalen; fail2ban blokerer gentagne fejlede forsøg | `journalctl`, `fail2ban-client status` | Adresseret |

Logningen dækker adgangskontrol (forbindelser og afviste logins), fejl og
systemhændelser. Hver logpost har tid, kilde (logmodul eller tjeneste),
niveau og en tekst, der beskriver hændelsen og resultatet.

**Ikke verificeret eller ikke dækket:**

- Backup og gendannelse samt konfigurationsændringer logges ikke. Enhedens
  konfiguration er bygget ind i firmwaren og kan ikke ændres under drift.
- Logposterne har ikke et fast hændelses-id eller et felt for kategori. Det
  fremgår kun af teksten og logmodulet.
- Vi har ikke gennemgået, om alle relevante hændelser i Mosquitto og på VPS'en
  logges med alle felterne.

**Afgrænsning:** Enhedens log er kun tilgængelig over den serielle
forbindelse, persisteres ikke og har uptime som tidsstempel. Der er ingen
central logopsamling. Lagring af audit-logs (CR 2.9), reaktion på audit-svigt
(CR 2.10) og ekstern adgang til logs (CR 6.1) er derfor ikke omfattet.

### CR 3.1 – Communication integrity

**Krav (7.3):** Komponenten skal beskytte integriteten af transmitteret
information. RE(1) kræver fra SL 2, at modtageren kan verificere, at
informationen er autentisk.

| Komponent | Implementering | Bevis | Status |
| --- | --- | --- | --- |
| Thingy:91 X | TLS på port 8883; brokerens certifikat og hostnavn verificeres; CA'en ligger i modemmets nøglelager | `CONFIG_MQTT_LIB_TLS` og `CONFIG_MQTT_HELPER_PORT=8883` i `Simple/app/prj.conf`; `provision_ca_certificate()` i `mqtt_client.c`; `peer_verify` `REQUIRED` og `hostname` i NCS' `mqtt_helper` | Opfyldt med RE(1) |
| Mosquitto | TLS 1.2 eller nyere; certifikat fra Let's Encrypt, fornyes af certbot | `tls_version tlsv1.2` i `mosquitto.conf` | Opfyldt |

- **Grundkravet:** TLS beskytter hver pakke med en integritetskontrol, så
  ændringer undervejs opdages og afvises.
- **RE(1):** Begge ender verificerer modparten. Enheden verificerer brokerens
  certifikat (Let's Encrypts rodcertifikat ISRG Root X1,
  `Simple/app/certs/ca-cert.pem`, i sec tag 42) og hostnavnet
  `aitsm.vps.webdock.cloud`. Brokeren autentificerer enheden med brugernavn og
  password (CR 1.2).
- **Ud over transporten:** NDATA sendes med QoS 1, og batchen slettes først,
  når brokeren har kvitteret med PUBACK (`handle_puback()` i `main.c`).
  SparkplugB's `seq` og `bdSeq` viser tabte eller gentagne beskeder, og QuestDB
  fjerner dubletter (`cloud/mqtt/migrations/001_enable_dedup.sql`).

**Afgrænsning:** Payloads er ikke signeret ende til ende, så integriteten
hviler på TLS mellem enhed og broker.

### CR 3.7 – Error handling

**Krav (7.9):** Fejltilstande skal identificeres og håndteres, uden at der
afsløres information, som en modstander kan udnytte. Grundkrav på alle niveauer.

| Komponent | Implementering | Bevis | Status |
| --- | --- | --- | --- |
| Thingy:91 X | Faste fejlbeskeder med numeriske koder og ingen credentials; backoff 5–60 s; PUBACK-timeout 120 s; fuld buffer springer nye målinger over og overskriver intet | `schedule_reconnect()` og `check_puback_timeout()` i `main.c`; test på hardware i [Verifikation](../Simple/README.md#verifikation) | Opfyldt |
| Ingest | Afviser ugyldige payloads uden at gå ned; prøver midlertidige skrivefejl igen med backoff | `DeliveryWorker` i `cloud/mqtt/ingest.py`; `cloud/mqtt/tests/` | Opfyldt |

Eksempler på fejlbeskeder fra enheden:

```c
LOG_ERR("Brokeren afviste forbindelsen, kode: %d", return_code);
LOG_ERR("Kunne ikke forbinde til %s: %d", hostname, err);
LOG_WRN("Ingen PUBACK efter %d s; lukker MQTT-forbindelsen", PUBACK_TIMEOUT_SECONDS);
```

Fejl ved opstart stopper enheden og viser rødt på LED'en.

**Afgrænsning:** Brokeren svarer med en MQTT-returkode ved afvist login. Det
er standardadfærd i MQTT og ikke noget, projektet har ændret.

### CR 4.1 – Information confidentiality

**Krav (8.3):** Komponenten skal beskytte fortroligheden af information i
transit og i lagret data. Grundkrav på alle niveauer.

| Komponent | Implementering | Bevis | Status |
| --- | --- | --- | --- |
| Thingy:91 X | TLS i transit; passwordet gives ved build og ligger ikke i repositoriet | `AITSM_MQTT_PASSWORD` i `Simple/app/CMakeLists.txt` | Delvist: password ukrypteret i flash |
| Mosquitto | Ukrypteret MQTT (1883) findes kun på det interne Docker-netværk | `backend` med `internal: true` i `cloud/docker/compose.yml` | Opfyldt |
| VPS | Ingest-credentials i `/etc/projekt-c/mqtt.env` (root, 600) | `env_file` i `compose.yml` | Opfyldt |
| QuestDB | Ikke udgivet på internettet: kun via værtens `127.0.0.1:9000` og containerne på `edge` og `backend` | `ports` og `networks` i `compose.yml` | Delvist: ikke krypteret på disk |

**Afgrænsning:** Den, der har fysisk adgang til enheden eller firmwarefilen,
kan læse passwordet. Data i QuestDB er ikke krypteret på disken. Begge dele er
en bevidst afgrænsning: projektet omfatter ikke avanceret nøglehåndtering, og
telemetrien er ikke følsom.

### EDR 3.14 – Integrity of the boot process

**Krav (13.9):** Embedded devices skal verificere integriteten af firmware,
software og konfiguration, før de bruges ved opstart. RE(1) kræver fra SL 2, at
verifikationen sker med producentens egen root of trust, så firmwarens
autenticitet er bevist.

| Komponent | Implementering | Bevis | Status |
| --- | --- | --- | --- |
| Thingy:91 X | Opstartskæde: NSIB verificerer MCUboot, og MCUboot verificerer applikationens signatur, før den køres. Konfigurationen fra `prj.conf` er bygget ind i applikationsimaget og dermed dækket | Test på boardet (herunder) | Opfyldt (SL 1) |

Projektet bruger Nordics standard-bootkæde i NCS uden ændringer. MCUboot, NSIB
og nøglerne er sat op i NCS' board-filer for Thingy:91 X
(`nrf/boards/nordic/thingy91x/Kconfig.sysbuild`).

Buildets konfiguration viser kun, hvordan buildet er sat op
(`SB_CONFIG_SECURE_BOOT_APPCORE=y`, `SB_CONFIG_BOOTLOADER_MCUBOOT=y`,
`SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y`). Vi flasher kun applikationen
(`dfu_application.zip`), så det er de NSIB og MCUboot, Nordic har flashet fra
fabrikken, der kører på boardet. Boardets opstartslog viser NCS v2.7.99 for
NSIB og v3.4.0 for applikationen. Den faktiske adfærd er derfor testet
på boardet.

**Test på boardet (2026-10-29):**

| # | Image | `imgtool verify` | Resultat på boardet |
| --- | --- | --- | --- |
| 1 | Gyldigt signeret Simple-image, hvor ét bogstav i logteksten `AITSM Simple starter` er ændret til `BITSM` | `Image has an invalid hash` | Uploadet (12 s). Applikationen starter ikke: ingen applikationslog i 45 s og ingen `BITSM Simple starter`. MCUboot skriver ingen log |
| 2 | Samme image uden ændringen (kontrol) | `Image was correctly validated` | Applikationen starter med det samme (`AITSM Simple starter`) |
| 3 | Det oprindelige image (gendannelse) | `Image was correctly validated` | Applikationen starter og forbinder til brokeren |

**Observeret:** Det ændrede image startede ikke, mens det samme image uden
ændringen startede med det samme. Den eneste forskel er ét bogstav i en
logtekst, som ikke kan få koden til at fejle.

**Konklusion, som testen understøtter:** MCUboot afviser imaget, fordi hashen
ikke passer. MCUboot fra fabrikken skriver ingen log, så der er ingen direkte
afvisningsmeddelelse, og testen beviser ikke alene, præcis hvorfor opstarten
stoppede.

NSIB skriver derimod i loggen. Ved opstarterne under testen viste den, at NSIB
verificerer MCUboots signatur:

```
Attempting to boot slot 0.
I: Verifying signature against key 0.
I: Firmware signature verified.
Booting (0x8200).
```

Sådan gentages testen fra `Simple/` med et build, der ikke er det, der kører
på boardet. nrfutil springer uploaden over, hvis imagets hash svarer til det
kørende image, også selvom indholdet er ændret:

```bash
mkdir -p /tmp/boottest && cd /tmp/boottest
unzip -o <build>/dfu_application.zip
python3 -c "d=bytearray(open('app.signed.bin','rb').read()); o=d.find(b'AITSM Simple starter'); d[o]=ord('B'); open('app.signed.bin','wb').write(d)"
python3 $ZEPHYR_BASE/../bootloader/mcuboot/scripts/imgtool.py verify \
  -k $ZEPHYR_BASE/../bootloader/mcuboot/root-ec-p256.pem app.signed.bin
zip -q tampered.zip app.signed.bin manifest.json
nrfutil device program --firmware tampered.zip --serial-number <id> \
  --traits mcuBoot --family nrf91 \
  --options target=nRF91,mcu_end_state=NRFDL_MCU_STATE_APPLICATION
```

Bagefter flashes det rigtige image igen som beskrevet i
[Original/README.md](../Original/README.md#flash-til-thingy91-x).

**Afgrænsning:** MCUboot på boardet accepterer images, der er signeret med NCS'
offentlige standardnøgle `bootloader/mcuboot/root-ec-p256.pem`. Det er vist
ved, at vores images starter. Verifikationen opdager ødelagt eller ændret
firmware, men beskytter ikke mod en angriber, der selv signerer firmware med
samme nøgle. NSIB's nøgle er sat fra fabrikken, og vi har ikke verificeret,
hvilken nøgle det er. RE(1) kræver egne nøgler (#70).

## Hvad et højere SL ville kræve

Medtages enhedens øvrige EDR-krav, ligger Thingy'en på SL-C 1, fordi flere af
dem først kræves fra SL 2. Det er forbedringer til et højere mål, ikke krav til
vores SL-T.

| Krav | Fra | Hvad der mangler | Issue |
| --- | --- | --- | --- |
| EDR 3.14 RE(1) Authenticity of the boot process | SL 2 | Egne signeringsnøgler til NSIB og MCUboot. Bootloaderne fra fabrikken skal da udskiftes, hvilket sandsynligvis kræver en debugger (ikke verificeret; afhænger af, hvilken nøgle NSIB bruger) | #70 |
| EDR 3.10 RE(1) Update authenticity and integrity | SL 2 | Samme: opdateringer er kun signeret med standardnøglen | #70 |
| EDR 3.12 Provisioning product supplier roots of trust | SL 2 | Egen root of trust skrevet til enheden | #70 |
| EDR 2.13 Use of physical diagnostic and test interfaces | SL 2 | Firmwaren låser ikke debug-porten selv (`CONFIG_NRF_APPROTECT_USE_UICR=y`, ikke `..._LOCK`). Tilstanden afhænger af UICR og er ikke verificeret på boardet | – |
| CR 1.2 RE(1) Unique identification and authentication | SL 3 | Unik identitet pr. enhed. En mulig løsning er mTLS med en nøgle genereret i modemmet | #71 |

## Minimum for det valgte SL-T

| Krav | Krævet til SL-T | Implementeret | Fil |
| --- | --- | --- | --- |
| CR 1.2 | grundkrav (conduit SL 2) | ✔ login, ingen anonyme, ACL | `Simple/app/src/mqtt_client.c`, `cloud/docker/mosquitto/mosquitto.conf` |
| CR 2.8 | grundkrav | delvist: logning af adgang, fejl og systemhændelser; ingen hændelses-id, og log på enheden er ikke persistent | `Simple/app/src/network.c`, `mqtt_client.c`, `main.c`, `cloud/mqtt/ingest.py` |
| CR 3.1 | RE(1) (conduit SL 2) | ✔ TLS på 8883, certifikat og hostnavn verificeres, PUBACK før sletning | `Simple/app/prj.conf`, `mqtt_client.c`, `main.c` |
| CR 3.7 | grundkrav | ✔ backoff, PUBACK-timeout, intet overskrives | `Simple/app/src/main.c`, `cloud/mqtt/ingest.py` |
| CR 4.1 | grundkrav | delvist: TLS i transit; password og database ukrypteret at rest | `Simple/app/CMakeLists.txt`, `cloud/docker/compose.yml` |
| EDR 3.14 | grundkrav | ✔ NSIB og MCUboot verificerer opstarten; testet på boardet | Nordics standard-bootkæde (`nrf/boards/nordic/thingy91x/Kconfig.sysbuild`) |

## CRA Annex I

### 1.c – Fortrolighed af data

> "protect the confidentiality of stored, transmitted or otherwise processed
> data […] such as by encrypting relevant data at rest or in transit by state
> of the art mechanisms"

- **I transit:** TLS 1.2 eller nyere mellem enhed og broker med verificeret
  servercertifikat (se CR 3.1 og CR 4.1). Ukrypteret MQTT findes kun på det
  interne Docker-netværk.
- **At rest:** CA-certifikatet ligger i modemmets nøglelager. MQTT-passwordet
  ligger ukrypteret i firmwaren, og QuestDB er ikke krypteret på disken. QuestDB
  kan ikke nås fra internettet, kun fra VPS'en selv og containerne på samme
  Docker-netværk.

**Status:** Opfyldt for data i transit. Delvist for data at rest.

### 1.d – Integritet og rapportering af fejl

> "protect the integrity of stored, transmitted or otherwise processed data
> […] commands, programs and configuration against any manipulation or
> modification not authorised by the user, as well as report on corruptions"

- **Data:** TLS beskytter integriteten i transit. Kun enheden må skrive til sine
  egne SparkplugB-topics. ACL'en på brokeren giver brugeren `thingy91x`
  skriveadgang til `spBv1.0/aitsm/NBIRTH/thingy91x` og
  `spBv1.0/aitsm/NDATA/thingy91x` og intet andet. Ingest må kun læse.
- **Kommandoer og konfiguration:** Enheden abonnerer ikke på noget topic, så
  den kan ikke styres eller omkonfigureres udefra. Konfigurationen ligger i
  `prj.conf` og bygges ind i firmwaren.
- **Rapportering:** Ugyldige payloads afvises af ingest og logges
  (`Rejected MQTT telemetry …`). Manglende PUBACK logges på enheden.
  SparkplugB's `seq` logges af ingest, så huller kan ses.
- **Programmer:** MCUboot verificerer applikationen ved opstart. På boardet
  startede et image med én ændret byte ikke, mens det uændrede image startede
  (se EDR 3.14).

**Status:** Delvist. MCUboot bruger NCS' standard-signeringsnøgle
(`bootloader/mcuboot/root-ec-p256.pem`), som er offentlig, så verifikationen
beskytter ikke mod en angriber, der selv bygger firmware. Egen nøgle følges i
#70.

### 1.h – Begræns angrebsfladen

> "be designed, developed and produced to limit attack surfaces, including
> external interfaces"

**Enheden:**

- Ingen lyttende sockets og ingen indgående forbindelser. Den eneste
  netværksforbindelse er den udgående TLS-forbindelse til egen broker.
- Ingen indgående kommandoer: Simple abonnerer ikke på MQTT-topics og har ikke
  NCMD.
- AT-shell og testprofiler fra Original er bevidst udeladt i Simple (se
  [Taget med og bevidst udeladt](../Simple/README.md#taget-med-og-bevidst-udeladt)).
- Reconnect med backoff (5 til 60 s) betyder, at enheden ikke belaster
  netværk og broker med hurtige forsøg, når brokeren er nede.

**Cloud:**

- Docker udgiver kun port 8883 på alle interfaces. QuestDB er kun udgivet på
  værtens `127.0.0.1`, og ingest har ingen udgivne porte og ingen adgang til
  internettet (`internal: true`).
- Ingest kører med read-only filsystem, uden Linux-capabilities
  (`cap_drop: [ALL]`) og med `no-new-privileges`.
- SSH accepterer kun nøgler (`PasswordAuthentication no`). fail2ban og
  automatiske sikkerhedsopdateringer (`unattended-upgrades`) er aktive.
- Docker-images er låst til en bestemt digest, så der kun køres testede
  versioner.

- Firewallen (UFW) tillader kun indgående SSH (22), HTTP/HTTPS (80/443) og
  MQTT over TLS (8883). Webdocks standard-FTP-server (pure-ftpd), som
  projektet ikke bruger, er stoppet og slået fra, og dens porte (21 og
  50000-50099) er fjernet fra firewallen (2026-10-21).

**Status:** Opfyldt. Den tilbageværende begrænsning er, at root-login med
nøgle er tilladt. Den kan fjernes, når alle administratorer bruger egne
brugere med sudo.

### 1.j – Registrering og overvågning af aktivitet

> "provide security related information by recording and/or monitoring
> relevant internal activity, including the access to or modification of data,
> services or functions"

- **Enheden** logger netværksstatus, forbindelse til og afvisning fra brokeren,
  afsendelse, kvittering og fejl (se CR 2.8).
- **Brokeren** logger, hvem der forbinder, og afviste logins.
- **Ingest** logger hver modtaget batch med enhed og `seq`, afviste payloads og
  fejl ved skrivning til databasen.
- **VPS'en** logger SSH-login i systemets journal, og fail2ban blokerer
  gentagne fejlede forsøg.

**Status:** Delvist. Aktiviteten registreres, men enhedens log forlader ikke
enheden, og der er ingen central overvågning eller alarmer.

### 2.1 – Identifikation af komponenter og sårbarheder (SBOM)

> "identify and document vulnerabilities and components contained in the
> product, including by drawing up a software bill of materials in a commonly
> used and machine-readable format covering at the very least the top-level
> dependencies of the product"

- **Firmware:** Der er en SBOM i SPDX 2.2 i
  [`docs/sbom/`](sbom/README.md), genereret med NCS' `west ncs-sbom` ud fra
  Simple's build. Den dækker applikationen, MCUboot og NSIB og lister de filer,
  der faktisk er linket ind (1281 filer), fordelt på pakker med repository og
  commit-hash: bl.a. Zephyr, `nrf_modem` (nrfxlib), MCUboot, mbedTLS, Oberon PSA
  crypto og nanopb. Afhængighederne er låst til NCS v3.4.0 via west-manifestet.
- **Cloud:** Mosquitto, QuestDB og ingest-imagets Python er låst til en digest
  i `cloud/docker/`, og ingest bruger `paho-mqtt==2.1.0`.
- **Sårbarheder:** Med SBOM'en kan det afgøres, om en ny sårbarhed, fx i
  mbedTLS, rammer firmwaren, fordi den præcise version står i filen.

**Status:** Opfyldt for komponenterne. SBOM'en skal genereres igen ved hver
release (kommandoen står i [`docs/sbom/README.md`](sbom/README.md)).
Sårbarhederne tjekkes ikke automatisk, og cloud-delen er dokumenteret med låste
versioner frem for en SPDX-fil.

## Opsummering

| Punkt | Status | Begrundelse |
|---|---|---|
| CR 1.2 – Device identification and authentication | Opfyldt (SL-C 2) | MQTT-login, ingen anonyme, ACL; delt password og fast klient-id |
| CR 2.8 – Auditable events | Delvist adresseret | Logging på enhed, broker, ingest og VPS; ingen persistent log på enheden |
| CR 3.1 – Communication integrity | Opfyldt (SL-C 2, RE(1)) | TLS med verificeret servercertifikat og hostnavn; PUBACK før sletning |
| CR 3.7 – Error handling | Grundkrav adresseret | Faste fejlkoder uden credentials; backoff, PUBACK-timeout, intet overskrives |
| CR 4.1 – Information confidentiality | Delvist | TLS i transit; password i flash og database ukrypteret at rest |
| EDR 3.14 – Integrity of the boot process | Opfyldt (SL-C 1) | Et ændret image starter ikke på boardet, det uændrede gør; standardnøgler, så ikke RE(1) (#70) |
| CRA 1.c – Fortrolighed | Opfyldt i transit, delvist at rest | TLS; password og database er ikke krypteret at rest |
| CRA 1.d – Integritet | Delvist | TLS, ACL, ingen indgående kommandoer; MCUboot med standardnøgle (#70) |
| CRA 1.h – Angrebsflade | Opfyldt | Kun udgående forbindelse fra enheden; firewall åbner kun 22, 80, 443 og 8883 |
| CRA 1.j – Registrering | Delvist | Logs på alle led; ingen central overvågning |
| CRA 2.1 – SBOM | Opfyldt | SPDX-SBOM for firmwaren i `docs/sbom/`; cloud-versioner låst med digest |
