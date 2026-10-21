# Standarder: IEC 62443-4-2 og CRA

Denne redegørelse dækker issue #11. Formålet er ikke fuld compliance, men at
udvælge relevante punkter fra IEC 62443-4-2 og CRA Annex I og argumentere for,
hvordan løsningen adresserer dem, og hvor grænserne går.

Redegørelsen beskriver afleveringsversionen [`Simple/`](../Simple/README.md) og
den fælles cloud i [`cloud/`](../cloud/). Original bygger på samme principper
(TLS, CA i modemmet, samme broker og ACL), men har flere moduler.

Kilder:

- IEC 62443-4-2: [`references/standards/iec-62443-4-2.pdf`](references/standards/iec-62443-4-2.pdf).
- CRA Annex I: de udvalgte paragraffer i
  [kravspecifikationen](references/requirements/ucl-mt-project-requirements-2026-08-26.pdf),
  som citerer Kommissionens forslag (15.9.2022). Nummereringen herunder følger
  kravspecifikationen. I den vedtagne forordning har punkterne andre bogstaver.

## Overblik over løsningen

```
Thingy:91 X ──MQTT/TLS :8883──▶ Mosquitto ──internt Docker-netværk──▶ ingest ──▶ QuestDB
(kun udgående)                  (Docker)                              (Docker)     (127.0.0.1)
```

- Enheden åbner kun én udgående forbindelse: MQTT over TLS til egen broker.
- Brokeren er den eneste tjeneste, der er udgivet fra Docker til internettet.
- Ingest og QuestDB kan ikke nås udefra.

## IEC 62443-4-2

### CR 3.1 – Communication integrity

**Krav:** Komponenten skal beskytte integriteten af transmitteret information.

**Hvordan løsningen adresserer det:**

- Al telemetri sendes over TLS på port 8883 (`CONFIG_MQTT_LIB_TLS`,
  `CONFIG_MQTT_HELPER_PORT=8883` i `Simple/app/prj.conf`). TLS beskytter hver
  pakke med en integritetskontrol, så ændringer undervejs opdages og afvises.
- Enheden verificerer brokerens certifikat. Let's Encrypts rodcertifikat
  (ISRG Root X1, `Simple/app/certs/ca-cert.pem`) skrives ved opstart ind i
  modemmets nøglelager under sec tag 42 med `modem_key_mgmt_write()`
  (`provision_ca_certificate()` i `Simple/app/src/mqtt_client.c`). NCS'
  `mqtt_helper` sætter `peer_verify` til `REQUIRED` og kontrollerer
  hostnavnet `aitsm.vps.webdock.cloud`, så en falsk broker med et andet
  certifikat afvises.
- Brokeren accepterer mindst TLS 1.2 (`tls_version tlsv1.2` i
  `cloud/docker/mosquitto/mosquitto.conf`). Certifikatet fornyes automatisk af
  certbot.
- Enheden autentificeres over for brokeren med brugernavn og password.
  Anonyme klienter afvises (`allow_anonymous false`).
- Ud over transporten sikrer applikationen, at data kommer frem i rette
  form: NDATA sendes med QoS 1, og batchen slettes først fra bufferen, når
  brokeren har kvitteret med PUBACK (`handle_puback()` i `main.c`).
  SparkplugB's `seq` og `bdSeq` gør det muligt at se tabte eller gentagne
  beskeder, og QuestDB fjerner dubletter
  (`cloud/mqtt/migrations/001_enable_dedup.sql`).

**Afgrænsning:** Brokeren autentificerer ikke enheden med certifikat (mTLS,
#71), kun med brugernavn og password. Payloads er ikke signeret ende til ende,
så integriteten hviler på TLS mellem enhed og broker.

### CR 4.1 – Information confidentiality

**Krav:** Beskyt fortroligheden af information på kommunikationskanaler og i
lagret data.

**Hvordan løsningen adresserer det:**

- Data i transit er krypteret med TLS mellem enhed og broker. Port 1883 uden
  TLS findes kun på det interne Docker-netværk mellem broker og ingest
  (`backend`, `internal: true` i `cloud/docker/compose.yml`) og udgives ikke
  på værten.
- MQTT-passwordet ligger ikke i repositoriet. Det gives som miljøvariabel
  (`AITSM_MQTT_PASSWORD`) ved build og skrives kun til den ignorerede
  build-mappe (`Simple/app/CMakeLists.txt`). Firmwaren nægter at starte uden
  password (`aitsm_mqtt_init()`).
- På serveren ligger ingest-credentials i `/etc/projekt-c/mqtt.env` (root,
  600), og QuestDB kan kun nås fra værten selv (`127.0.0.1:9000`).

**Afgrænsning:** Passwordet bliver en del af firmwarefilen og ligger
ukrypteret i enhedens flash. Den, der har fysisk adgang til enheden eller
firmwarefilen, kan læse det. Data i QuestDB er ikke krypteret på disken.
Begge dele er en bevidst afgrænsning: projektet omfatter ikke avanceret
nøglehåndtering, og telemetrien (temperatur og batteri) er ikke følsom.

### CR 3.7 – Error handling

**Krav:** Fejltilstande skal identificeres og håndteres, uden at der afsløres
information, som en modstander kan udnytte.

**Hvordan løsningen adresserer det:**

- Fejl logges med korte, faste beskeder og numeriske fejlkoder. Credentials
  logges aldrig; kun hostnavnet nævnes:

  ```c
  LOG_ERR("Brokeren afviste forbindelsen, kode: %d", return_code);
  LOG_ERR("Kunne ikke forbinde til %s: %d", hostname, err);
  LOG_WRN("Ingen PUBACK efter %d s; lukker MQTT-forbindelsen", PUBACK_TIMEOUT_SECONDS);
  ```

- Fejl fører til en kontrolleret tilstand i stedet for et nedbrud:
  - Mistet forbindelse: nye forsøg efter 5, 10, 20, 40 og derefter højst 60 s
    (`schedule_reconnect()` i `main.c`).
  - Død forbindelse uden fejlmelding fra modemmet: udebliver PUBACK i 120 s,
    lukkes forbindelsen, og batchen sendes igen efter reconnect
    (`check_puback_timeout()`).
  - Fuld buffer: nye målinger springes over og tælles i loggen. Data, der
    venter på at blive sendt, overskrives aldrig.
  - Fejl ved opstart: enheden stopper og viser rødt på LED'en.
- I cloud afviser ingest ugyldige payloads med en advarsel uden at gå ned, og
  midlertidige skrivefejl til QuestDB prøves igen med backoff
  (`DeliveryWorker` i `cloud/mqtt/ingest.py`).
- Adfærden er testet på Thingy'en med broker-udfald og blokeret forbindelse
  (se [Verifikation i Simple](../Simple/README.md#verifikation)).

**Afgrænsning:** Brokeren svarer med en MQTT-returkode ved afvist login.
Det er standardadfærd i MQTT og ikke noget, projektet har ændret.

### CR 2.8 – Auditable events

**Krav:** Komponenten skal kunne generere audit-records for bl.a. adgang,
fejl og systemhændelser med tid, kilde, type og resultat.

**Hvordan løsningen adresserer det:**

- Enheden logger med Zephyrs logging-subsystem, ét logmodul pr. fil
  (`LOG_MODULE_REGISTER`): netværksstatus og tildelt PSM (`network.c`),
  forbindelse, afvisning og lukning af MQTT (`mqtt_client.c`) samt afsendelse,
  PUBACK, timeout og oversprungne målinger (`main.c`).
- Mosquitto logger forbindelser og afviste logins, og ingest logger
  modtagne batches, afviste payloads og skrivefejl. Begge logs kan ses med
  `docker compose logs` på VPS'en.

**Afgrænsning:** Enhedens log er kun tilgængelig over den serielle
forbindelse, persisteres ikke og har uptime som tidsstempel. Der er ingen
central logopsamling. Kravene til lagring af audit-logs (CR 2.9), reaktion på
audit-svigt (CR 2.10) og ekstern adgang til logs (CR 6.1) er derfor ikke
omfattet.

## CRA Annex I

### 1.c – Fortrolighed af data

> "protect the confidentiality of stored, transmitted or otherwise processed
> data […] such as by encrypting relevant data at rest or in transit by state
> of the art mechanisms"

- **I transit:** TLS 1.2 eller nyere mellem enhed og broker med verificeret
  servercertifikat (se CR 3.1 og CR 4.1). Ukrypteret MQTT findes kun på det
  interne Docker-netværk.
- **At rest:** CA-certifikatet ligger i modemmets nøglelager. MQTT-passwordet
  ligger ukrypteret i firmwaren, og QuestDB er ikke krypteret på disken, men kan
  kun nås fra VPS'en selv.

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
- **Programmer:** Firmwaren bygges med NCS' sysbuild og MCUboot, som
  verificerer signaturen på applikationen ved opstart.

**Status:** Delvist. MCUboot bruger NCS' standard-signeringsnøgle
(`bootloader/mcuboot/root-ec-p256.pem`), som er offentlig, så verifikationen beskytter ikke mod en angriber, der selv bygger
firmware. Egen nøgle følges i #70.

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

- Docker udgiver kun port 8883 på alle interfaces. QuestDB er bundet til
  `127.0.0.1`, og ingest har ingen udgivne porte og ingen adgang til
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
| CR 3.1 – Communication integrity | Opfyldt | TLS med verificeret servercertifikat og hostnavn; PUBACK før sletning |
| CR 4.1 – Information confidentiality | Delvist | TLS i transit; password ukrypteret i firmwaren |
| CR 3.7 – Error handling | Opfyldt | Faste fejlkoder uden credentials; backoff, PUBACK-timeout, intet overskrives |
| CR 2.8 – Auditable events | Delvist | Logging på enhed, broker og ingest; ingen persistent log på enheden |
| CRA 1.c – Fortrolighed | Opfyldt i transit, delvist at rest | TLS; password og database er ikke krypteret at rest |
| CRA 1.d – Integritet | Delvist | TLS, ACL, ingen indgående kommandoer; MCUboot med standardnøgle (#70) |
| CRA 1.h – Angrebsflade | Opfyldt | Kun udgående forbindelse fra enheden; firewall åbner kun 22, 80, 443 og 8883 |
| CRA 1.j – Registrering | Delvist | Logs på alle led; ingen central overvågning |
| CRA 2.1 – SBOM | Opfyldt | SPDX-SBOM for firmwaren i `docs/sbom/`; cloud-versioner låst med digest |
