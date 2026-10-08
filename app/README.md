# AITSM cellular connection

Zephyr-app til Nordic Thingy:91 X (`thingy91x/nrf9151/ns`). Applikationen
initialiserer nRF9151-modemet og etablerer en NB-IoT-forbindelse via SIM-kortet.
Modemet er konfigureret til udelukkende at bruge NB-IoT (issue #25); LTE-M og
kombinationsmoden er deaktiveret, så enheden aldrig falder tilbage til LTE-M.
Ved modeminitialisering provisioneres den offentlige Let’s Encrypt CA til
nRF9151-modemmet. Efter LTE-registrering oprettes en TLS-sikret MQTT-forbindelse
til `aitsm.vps.webdock.cloud:8883`.

RGB-LED'en viser forbindelses- og afsendelsesstatus:

| Status | Farve og mønster |
| --- | --- |
| Søger efter LTE | Blå, blinkende |
| LTE forbundet | Grøn, fast |
| Cloud MQTT forbundet | Cyan, fast |
| Ikke forbundet / generel fejl | Rød, fast |
| Måling sendt korrekt | 5 korte hvide blink |
| Fejl ved afsendelse | 5 korte røde blink |

Et midlertidigt succes- eller fejlblink (fem korte hvide/røde blink) vises og
vender derefter automatisk tilbage til den seneste stabile status —fx cyan, hvis
MQTT stadig er forbundet. Al farvevalg og blinklogik ligger centralt i
`src/led_status.c`, så netværks- og MQTT-laget kun angiver en status gennem
`led_status_set()`.

## Krav til SIM-kort og operatør

Fordi enheden kun kan bruge NB-IoT, skal både SIM-kortet og mobiloperatøren
understøtte NB-IoT, før en forbindelse kan etableres:

- SIM-kortet skal være et IoT-abonnement med NB-IoT (et ren LTE-M- eller
  tale-/dataabonnement uden NB-IoT virker ikke).
- Operatøren skal have NB-IoT-dækning på det sted, enheden skal stå.
- Ved forbindelsesproblemer: kontrollér først operatørens NB-IoT-dækning og
  abonnementet, og se derefter i den serielle log om modemmet rapporterer
  `LTE mode: NB-IoT` og `LTE registered ...`.

Boardet bygges som **non-secure (`/ns`)**, fordi `CONFIG_NRF_MODEM_LIB` kræver
`CONFIG_TRUSTED_EXECUTION_NONSECURE=y`, som kun sættes af `ns`-varianten af
boardet. Se [`KCONFIG.md`](KCONFIG.md) for en gennemgang af hver enkelt
Kconfig-indstilling i `prj.conf`.

## Mappens filer

- `CMakeLists.txt` kobler applikationen sammen med Zephyr-buildsystemet.
- `prj.conf` aktiverer PWM, nRF-modem, LTE Link Control, IP-socket-offload og
  MQTT-understøttelse (se [`KCONFIG.md`](KCONFIG.md)).
- `Kconfig` indeholder den centrale konfiguration for måleinterval, single- og
  batch-afsendelse samt faste buffergrænser.
- `include/led_status.h` deklarerer LED-statusmodulets API og statusser.
- `include/app_controller.h` deklarerer events mellem netværkslagene og
  applikationslogikken.
- `include/data_transmission.h` deklarerer målebufferens og payload-lagets API.
- `include/network.h` deklarerer netværksmodulets API.
- `src/app_controller.c` er applikationslogikken og dens message queue.
- `src/data_transmission.c` opbevarer målinger i en fast buffer og formaterer
  dem som SparkplugB NDATA (single eller batch).
- `src/sparkplug.c` og `include/sparkplug.h` laver SparkplugB-topics og
  protobuf-payloads (NBIRTH og NDATA) med nanopb. `proto/` indeholder
  protobuf-definitionen og de genererede nanopb-filer.
- `src/measurement_service.c` styrer uafhængig måle- og afsendelsesplanlægning.
- `src/measurement_source.c` læser modemtemperatur, nPM1300-batterispænding
  og UTC-tid.
- `src/led_status.c` styrer RGB-LED'en.
- `src/mqtt_client.c` opretter MQTT/TLS-forbindelsen efter LTE-registrering.
- `src/mqtt_reconnect.c` styrer reconnect og eksponentiel backoff.
- `src/network.c` initialiserer modemmet, starter LTE-forbindelsen og reagerer på
  ændringer i netværksregistreringen.
- `src/main.c` initialiserer modulerne og starter måleplanlægning efter modemmet.

## Tråd- og eventstruktur

LTE- og MQTT-bibliotekerne arbejder asynkront. Deres callbacks udfører derfor
kun let behandling og lægger events i applikationslogikkens message queue.
Events behandles i Zephyrs system-workqueue, som er en separat Zephyr-
trådkontekst. Den håndterer forbindelsesstatus og lægger MQTT-connect i en
dedikeret, preemptiv MQTT-workqueue efter LTE-registrering. DNS, TCP, TLS og
publish kan dermed blokere uden at forsinke app-events og måleservicens
arbejde på systemworkqueue. Workeren har som standard 4096 bytes stack og
prioritet 5; se [`KCONFIG.md`](KCONFIG.md).

Publish kopierer payloaden til MQTT-workerens egen buffer, så måleservicen
kan genbruge sin buffer uden at ændre en igangværende socket-skrivning. Der
accepteres højst ét ventende publish. Queue-accept er ikke en leveringskvittering:
afsendelsesfejl og brokerens PUBACK returneres som app-events med et lokalt
afsendelses-token. Måleservicen afviser resultater fra tidligere afsendelser.

Mangler PUBACK efter en vellykket skrivning, sender MQTT-workeren samme
payload og message-id igen med DUP-flag efter 30 s. Ventetiden fordobles til
højst 300 s. ACK og disconnect annullerer timeren; nye målinger flytter ikke
deadline. Genforsøg bevarer TLS-forbindelsen og kræver ingen ekstra MQTT-felter.
Se [genforsøg og validering](../docs/puback-genforsoeg.md).

Ved MQTT-afbrud eller connect-fejl prøver controlleren igen efter 5, 10, 20,
40 og højst 60 sekunder, så længe LTE er registreret. MQTT-CONNACK nulstiller
backoff. LTE-tab annullerer timeren og stopper afsendelse straks; ved ny
LTE-registrering startes et forsøg med det samme. Dublerede fejl- og
disconnect-events flytter ikke den allerede planlagte deadline. Reconnect
kræver dermed ingen genstart eller ny LTE-registrering efter broker-genstart.

MQTT-klienten afviser connect/publish uden LTE, beskytter mod parallelle
connect-forsøg og ignorerer PUBACK fra tidligere publish/sessioner. En sen
CONNACK efter LTE-tab starter ikke målinger; forbindelsen lukkes på
netværkskøen. Et allerede igangværende DNS/TCP/TLS-kald kan først afsluttes,
når helperen returnerer; systemworkqueue behandler fortsat LTE-tab imens.

Afviste eller afbrudte MQTT-forbindelser, helper-fejl og publish-resultater
følger samme event-flow og logges centralt af applikationscontrolleren.
Måleservicen beholder bufferen ved fejl og sletter først målingerne efter et
vellykket MQTT-acknowledgement.

## Data- og transmissionsprofil

Standardprofilen er måling hvert 15. sekund og batching med en maksimal
afsendelsesfrekvens på fem minutter. En batch indeholder 20 målinger, svarende til fem minutters data, men
bufferen har plads til 28, se "Hvorfor 28 pladser" nedenfor. Single-afsendelse kan
vælges i Kconfig til test og fejlsøgning.

Indstillingerne ændres centralt i `prj.conf` eller via et overlay:

- `CONFIG_AITSM_MEASUREMENT_INTERVAL_SECONDS`: 5-15 sekunder.
- `CONFIG_AITSM_TRANSMISSION_BATCH` eller
  `CONFIG_AITSM_TRANSMISSION_SINGLE`.
- `CONFIG_AITSM_BATCH_INTERVAL_SECONDS`: standard 300 sekunder.
- `CONFIG_AITSM_BATCH_MAX_SAMPLES`: standard 28 målinger.

Bufferen har fast størrelse og afviser nye målinger, når den er fuld. API'et
understøtter først at fjerne målinger efter en vellykket MQTT-acknowledgement,
så afsendelseslaget kan beholde data ved forbindelsesfejl. Payloaden er
SparkplugB v1.0, se næste afsnit.

## SparkplugB (#66)

Topic-namespace er `spBv1.0/<group_id>/<message_type>/<edge_node_id>`, her
`spBv1.0/aitsm/NDATA/thingy91x` (gruppe og node kan sættes i Kconfig).
Thingy:91 X er edge node; der er ikke noget device-niveau under den.

- **NBIRTH** sendes ved hver MQTT-forbindelse og skal være kvitteret (PUBACK),
  før den første NDATA sendes. Den har seq 0, metric `bdSeq` (tæller
  forbindelser siden opstart) og deklarerer metrics `temperature` og `battery`
  uden værdi.
- **NDATA** har seq 1, 2, ... (wrap efter 255) og metrics `temperature` (°C) og
  `battery` (%) som Float med målingens tidsstempel i millisekunder (UTC).
  Batch markeres `is_historical`, fordi målingerne er gemt og sendt senere.
  Seq tælles først op, når PUBACK er modtaget, så et genforsøg efter tabt
  PUBACK har samme seq.
- **NDEATH** er bevidst udeladt: NCS' `mqtt_helper` kan kun sætte et statisk
  last will som Kconfig-tekst (QoS 0), og en protobuf-payload kan ikke udtrykkes
  som tekst. Skyen ser derfor en afbrudt enhed som manglende data, ikke som en
  NDEATH. Ingen NCMD/NDEATH-håndtering er derfor nødvendig i ingest.
- Protobuf-definitionen er en delmængde af Sparkplug B (samme feltnumre), så
  alle standard-klienter kan læse beskederne. Se `proto/README.md` for
  regenerering.
- `CONFIG_AITSM_TRANSMISSION_PAYLOAD_SIZE` skal kunne rumme en fuld batch
  (op til ca. 70 bytes pr. måling); et `BUILD_ASSERT` afviser ugyldige
  kombinationer ved build.

Batteriværdien er i første version et lineært estimat ud fra nPM1300-
batterispændingen (3,2 V = 0 % og 4,2 V = 100 %). Det er tilstrækkeligt til
pipeline-test, men bør kalibreres eller erstattes af en egentlig fuel-gauge-
model før præcis batterirapportering.

### Hvorfor 28 pladser i bufferen

En batch sendes efter 20 målinger (5 minutter), men bufferen har plads til 28.
De 8 ekstra pladser er et sikkerhedsnet:

1. Når en batch er sendt, bliver målingerne i bufferen, indtil serveren har
   kvitteret. Først da må de slettes, så ingen data går tabt.
2. Målingerne stopper ikke imens: der kommer en ny hvert 15. sekund, og de lægges
   i de ledige pladser.
3. Hvis kvitteringen er forsinket, er der ellers ingen plads til de nye
   målinger, og de kasseres. Med præcis 20 pladser skete det i en test, da
   kvitteringen var mere end 15 sekunder om at komme.
4. 8 ekstra pladser × 15 sekunder = 2 minutter. Så længe må kvitteringen vare,
   uden at vi taber en måling. Den længste leveringstid, vi målte, var under 1
   minut ved batch hvert minut.

28 er også den største buffer, der stadig kan være i en payload på 2048 bytes
(28 × 70 bytes + 16 = 1976). Større buffer kræver større payload og mere RAM.
Afsendelsen sker stadig efter 20 målinger, fordi tidsgrænsen er den, der
udløser den.

## Målinger under MQTT-udfald (#77)

Målinger starter efter modeminitialisering og fortsætter på den normale
15-sekunders cadence, også mens MQTT eller LTE er nede. Afsendelse er en
separat opgave, som kun kører med MQTT-forbindelse. Reconnect sender de
bufferede målinger samlet; PUBACK fjerner kun den kvitterede del, så nyere
målinger fra ventetiden bliver liggende.

Den faste RAM-buffer beskytter de ældste, ubekræftede målinger. Når den er
fuld, springes nye målepladser over og tælles; ingen data overskrives, og der
udføres ingen sensor-/AT-læsninger, som ikke kan gemmes. Et fuldt-buffer-forløb
logges samlet. Målingerne genoptages på den normale cadence efter frigivet plads.

Med standardprofilen er der plads til 28 målinger (ca. syv minutters
målepladser i en tom buffer); eksisterende data reducerer pladsen under et
udfald. Single-profilen har én plads. RAM-data og tabstæller nulstilles ved
reboot/strømsvigt. Før UTC-tiden er gyldig gemmes ingen udaterede målinger.

Dette vælger mulighed A i #77: lokal indsamling og buffering, med en tydelig
grænse for tab. Det kræver ingen ekstra radioopkoblinger eller flash-skrivninger.
Læs [beslutningen og valideringen](../docs/maalinger-under-udfald.md).
Dataforbrug og leveringstid behandles i #32. Strømmåling med PPK2 følges i #99.

## Strømbesparelse (#23)

Strømforbruget holdes nede på to niveauer:

- **Modemmet** beder om PSM (`CONFIG_LTE_PSM_REQ`): ønsket TAU 3600 s og aktiv
  tid 10 s. På den testede NB-IoT-forbindelse (roaming) tildelte nettet TAU
  4200 s og aktiv tid 10 s, og det logges som `PSM tildelt: ...`. Nettet kan
  tildele andre værdier hos andre operatører; en aktiv tid på -1 betyder, at
  PSM ikke er tildelt. Der bruges ikke eDRX: enheden sender kun og modtager
  ingen beskeder fra skyen (NCMD er ikke med), så eDRX ville kun forlænge den
  tid, modemmet lytter.
- **MQTT-keepalive** er 1200 s (`CONFIG_MQTT_KEEPALIVE`, standard 60 s) og længere
  end batch-intervallet på 300 s. Ellers ville ping hvert minut holde modemmet
  vågent og gøre PSM virkningsløs. Selve batchene holder forbindelsen og
  operatørens NAT-mapping i live. Mosquitto afbryder først efter 1,5 gange
  keepalive uden trafik.
- **MCU'en** sover mellem målingerne: måleplanlægningen bruger `k_work_delayable`
  på en tickless kernel, så CPU'en står i idle, når intet arbejde er klar, og
  vågner ved næste måleinterval eller netværkshændelse. Der er ingen
  busy-wait.

Målt forløb på enheden med standardprofilen (se `RRC-tilstand` i loggen): RRC
er *connected* i ca. 12 sekunder, når en batch sendes hvert 5. minut, og *idle*
resten af tiden. Der er ingen keepalive-ping imellem. Dataforbruget behandles i
#32; måling af selve strømforbruget afventer PPK2 i #99.

`overlay-low-power.conf` slår konsol og logning fra til strømmålinger:

```bash
west build -b thingy91x/nrf9151/ns -d build/lowpower app -- -DEXTRA_CONF_FILE=overlay-low-power.conf
```

## Logning

Alle moduler logger via Zephyrs logging-subsystem (`LOG_INF`/`LOG_WRN`/`LOG_ERR`/
`LOG_DBG`) med tidsstempler til den serielle USB-konsol. Niveauet styres centralt
med `CONFIG_AITSM_LOG_LEVEL` (0 = off, 1 = error, 2 = warning, 3 = info,
4 = debug); standard er 3. Sæt den til 4 i `prj.conf` eller et overlay for mest
mulig detalje lokalt:

```text
CONFIG_AITSM_LOG_LEVEL=4
```

LTE- og MQTT-hændelser samt fejl logges på info/warning/error, og hver
indsamlede måling logges på info med temperatur og batteri. Debug-niveauet
tilføjer blandt andet LTE-modeopdateringer.

## Build

Fra projektroden:

```bash
source scripts/activate-ncs.sh
west build -b thingy91x/nrf9151/ns -d build/thingy91x_nrf9151 app
```

Før build skal MQTT-passwordet sættes lokalt, eksempelvis fra en lokal secret
manager:

```bash
export AITSM_MQTT_USERNAME=thingy91x
export AITSM_MQTT_PASSWORD='password-fra-secret-manager'
```

Passwordet bliver ikke gemt i repository’et.

Buildet genererer blandt andet `build/thingy91x_nrf9151/dfu_application.zip`.

## Flash via USB/MCUboot

Find først boardets device-id:

```bash
nrfutil device list
```

Upload derefter ZIP-filen med det id, der blev vist:

```bash
nrfutil device program \
  --firmware build/thingy91x_nrf9151/dfu_application.zip \
  --serial-number THINGY91X_XXXXXXXXXXXX \
  --traits mcuBoot \
  --family nrf91 \
  --options target=nRF91,mcu_end_state=NRFDL_MCU_STATE_APPLICATION
```

Åbn en seriel terminal efter flash. Ved en vellykket forbindelse vises
`LTE registered ...` og `LTE mode: NB-IoT` i loggen. LED'en blinker blå, mens
der søges, lyser grøn ved LTE-registrering og skifter til cyan, når
MQTT-forbindelsen er oppe. Ved mistet forbindelse bliver den rød, mens modemmet
forsøger at genoprette forbindelsen. Fem korte hvide blink bekræfter en afsendt
måling, og fem korte røde blink viser en afsendelsesfejl. Logger modemmet en
uventet LTE-mode (fx LTE-M), er konfigurationen eller netværket ikke
NB-IoT-kompatibelt.

## Test

Se [`../tests/README.md`](../tests/README.md) for den automatiserede test, der
verificerer NB-IoT-konfigurationen:

```bash
source scripts/activate-ncs.sh
west twister -p thingy91x/nrf9151/ns -T tests
```
