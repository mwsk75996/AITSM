# AITSM cellular connection

Zephyr-app til Nordic Thingy:91 X (`thingy91x/nrf9151/ns`). Applikationen
initialiserer nRF9151-modemet og etablerer en NB-IoT-forbindelse via SIM-kortet.
Modemet er konfigureret til udelukkende at bruge NB-IoT (issue #25); LTE-M og
kombinationsmoden er deaktiveret, så enheden aldrig falder tilbage til LTE-M.
Ved modeminitialisering provisioneres den offentlige Let’s Encrypt CA til
nRF9151-modemmet. Efter LTE-registrering oprettes en TLS-sikret MQTT-forbindelse
til `aitsm.vps.webdock.cloud:8883`.

RGB-LED'en viser forbindelsesstatus:

- Blå: søger efter LTE-netværk eller forbinder.
- Grøn: NB-IoT-forbindelsen er aktiv.
- Rød: ingen forbindelse eller fejl.

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
- `include/led_status.h` deklarerer LED-statusmodulets API.
- `include/app_controller.h` deklarerer events mellem netværkslagene og
  applikationslogikken.
- `include/data_transmission.h` deklarerer målebufferens og payload-lagets API.
- `include/network.h` deklarerer netværksmodulets API.
- `src/app_controller.c` er applikationslogikken og dens message queue.
- `src/data_transmission.c` opbevarer målinger i en fast buffer og formaterer
  single- eller batch-payloads.
- `src/led_status.c` styrer RGB-LED'en.
- `src/mqtt_client.c` opretter MQTT/TLS-forbindelsen efter LTE-registrering.
- `src/network.c` initialiserer modemmet, starter LTE-forbindelsen og reagerer på
  ændringer i netværksregistreringen.
- `src/main.c` initialiserer LED- og netværksmodulerne.

## Tråd- og eventstruktur

LTE- og MQTT-bibliotekerne arbejder asynkront. Deres callbacks udfører derfor
kun let behandling og lægger events i applikationslogikkens message queue.
Events behandles i Zephyrs system-workqueue, som er en separat Zephyr-
trådkontekst. Den håndterer forbindelsesstatus og starter MQTT efter en
vellykket LTE-registrering. På den måde deles callback-data ikke direkte
mellem modulerne.

Afviste eller afbrudte MQTT-forbindelser, helper-fejl og publish-resultater
følger samme event-flow og logges centralt af applikationscontrolleren. Den
konkrete reconnect-strategi og genafsendelse af buffrede målinger kobles på,
når afsendelsesbufferen implementeres.

Måle- og batchinglogik implementeres separat. Denne struktur fastlægger kun
modulernes ansvar og synkroniseringen mellem netværkslagene og applikationen.

## Data- og transmissionsprofil

Standardprofilen er måling hvert 15. sekund og batching med en maksimal
afsendelsesfrekvens på fem minutter. Batch-bufferen kan indeholde 20 målinger,
svarende til fem minutters data ved standardintervallet. Single-afsendelse kan
vælges i Kconfig til test og fejlsøgning.

Indstillingerne ændres centralt i `prj.conf` eller via et overlay:

- `CONFIG_AITSM_MEASUREMENT_INTERVAL_SECONDS`: 5-15 sekunder.
- `CONFIG_AITSM_TRANSMISSION_BATCH` eller
  `CONFIG_AITSM_TRANSMISSION_SINGLE`.
- `CONFIG_AITSM_BATCH_INTERVAL_SECONDS`: standard 300 sekunder.
- `CONFIG_AITSM_BATCH_MAX_SAMPLES`: standard 20 målinger.

Bufferen har fast størrelse og afviser nye målinger, når den er fuld. API'et
understøtter først at fjerne målinger efter en vellykket MQTT-acknowledgement,
så afsendelseslaget kan beholde data ved forbindelsesfejl. Den nuværende
serialisering er et internt JSON-transportformat; SparkplugB-serialiseringen
kan udskiftes bag `data_transmission`-API'et.

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
`LTE registered ...` og `LTE mode: NB-IoT` i loggen. LED'en lyser grøn, når
NB-IoT-forbindelsen er aktiv. Før registrering lyser LED'en blå; ved mistet
forbindelse bliver den rød, mens modemmet forsøger at genoprette forbindelsen.
Logger modemmet en uventet LTE-mode (fx LTE-M), er konfigurationen eller
netværket ikke NB-IoT-kompatibelt.

## Test

Se [`../tests/README.md`](../tests/README.md) for den automatiserede test, der
verificerer NB-IoT-konfigurationen:

```bash
source scripts/activate-ncs.sh
west twister -p thingy91x/nrf9151/ns -T tests
```
