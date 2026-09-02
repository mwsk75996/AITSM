# Kconfig-indstillinger i `prj.conf`

Denne fil forklarer hver `CONFIG_*`-indstilling i [`prj.conf`](prj.conf): hvad
den gør, og hvorfor den er slået til for Projekt C (Thingy:91 X / nRF9151).
Kilder er NCS v3.4.0 / Zephyr 4.4.0's egne Kconfig-filer.

## GPIO og PWM (RGB-LED)

### `CONFIG_GPIO=y`
Slår GPIO-driveren til. Bruges indirekte af PWM-output til RGB-LED'en.

### `CONFIG_PWM=y`
Slår PWM-driveren (Pulse Width Modulation) til. `led_status.c` bruger tre
PWM-kanaler til at styre RGB-LED'en efter forbindelsesstatus.

## Netværk

### `CONFIG_NETWORKING=y`
Zephyrs overordnede tænd/sluk-knap for link-lag og IP-netværk. Alle øvrige
netværksindstillinger (`NET_SOCKETS`, `MQTT_LIB` osv.) ligger inde i et
`if NETWORKING`-blok i Zephyrs kildekode og er derfor ikke tilgængelige, før
denne er slået til. Trækker automatisk `NET_BUF`, `POLL` og
`ENTROPY_GENERATOR` med sig.

### `CONFIG_NET_SOCKETS=y`
Aktiverer BSD Sockets-API'et (`socket()`, `connect()`, `send()` osv.) oven på
Zephyrs netværksstak. Det er dette API, MQTT-biblioteket og applikationens
egen netværkskode bruger til at sende/modtage data.

## Logging

### `CONFIG_LOG=y`
Den globale tænd/sluk-knap for Zephyrs logging-framework. Uden den bliver
`LOG_INF()`/`LOG_ERR()`-kald slet ikke kompileret ind i firmwaren. Nødvendig
for at kunne fejlsøge LTE-forbindelse og MQTT senere.

## MQTT

### `CONFIG_MQTT_LIB=y`
Zephyrs indbyggede MQTT-klientbibliotek (oven på `NET_SOCKETS`). Bemærk:
kravspecifikationen omtaler den som `CONFIG_MQTT`, men det faktiske
Kconfig-symbol i Zephyr 4.4.0 hedder `MQTT_LIB`
(`zephyr/subsys/net/lib/mqtt/Kconfig`). `MQTT_LIB` vælger selv `NET_SOCKETS`,
men den er sat eksplicit her for tydelighed.

### `CONFIG_MQTT_HELPER=y`
NCS-specifik hjælper (`nrf/subsys/net/lib/mqtt_helper`) der binder
`MQTT_LIB` sammen med TLS og modemmets sockets på en måde der matcher Nordics
eksempler og biblioteker (f.eks. certifikat-provisionering, sikre
forbindelser via `sec_tag`). Gør det simplere at oprette en TLS-sikret
MQTT-forbindelse end at bruge `MQTT_LIB` direkte.

`MQTT_HELPER` bruges her sammen med TLS-konfigurationen nedenfor til den reelle
sikre forbindelse. Certifikatfilen er den offentlige Let’s Encrypt CA; ingen
server- eller klient-private nøgler er nødvendige for denne broker.

### TLS og MQTT-helper

`CONFIG_MQTT_LIB_TLS=y` aktiverer MQTT over TLS. `CONFIG_MQTT_HELPER_PORT=8883`
vælger brokerens TLS-port, og `CONFIG_MQTT_HELPER_SEC_TAG=42` er den interne
security tag, som certifikatet registreres under. `CONFIG_MODEM_KEY_MGMT=y`
gør det muligt at registrere den offentlige Let’s Encrypt CA direkte i
nRF9151-modemmet ved opstart, hvilket er den metode Nordics nRF91-sample bruger.
Der lægges ingen privat nøgle eller MQTT-password i repository’et.

Ved et lokalt build skal MQTT-passwordet leveres via miljøvariablen
`AITSM_MQTT_PASSWORD`; CMake genererer derefter en header i den ignorerede
build-mappe. Mangler passwordet, stopper firmwaren før LTE-forbindelsen startes.

## Cellulær forbindelse (nRF9151-modem)

### `CONFIG_NRF_MODEM_LIB=y`
Nordics modembibliotek – broen mellem applikationen og selve
LTE-modemet i nRF9151. Kræver
`CONFIG_TRUSTED_EXECUTION_NONSECURE=y`, hvilket er årsagen til at boardet
bygges som `thingy91x/nrf9151/ns` (se rod-README). Trækker selv
`NET_SOCKETS_OFFLOAD`, `NET_IPV4` og `NET_IPV6` ind (via `imply`), fordi
IP-stakken reelt køres af modemet – Zephyrs socket-API bruges kun som
grænseflade oven på modemmets offloadede sockets.

### `CONFIG_LTE_LINK_CONTROL=y`
NCS-bibliotek til at styre og overvåge LTE-forbindelsen: opkobling,
netværksregistrering, PSM/eDRX, cellemålinger m.m. Bruges til selve
opkoblingslogikken, adskilt fra `NRF_MODEM_LIB` som blot giver adgang til
modemmet.

### `CONFIG_LTE_NETWORK_MODE_NBIOT=y`
Vælger netværksmoden NB-IoT i lte_lc's `LTE_NETWORK_MODE`-choice
(`nrf/lib/lte_link_control/Kconfig`). Choiceens standardværdi er
`LTE_NETWORK_MODE_LTE_M_NBIOT_GPS` (LTE-M, NB-IoT og GNSS), men siden issue #25
skal enheden **udelukkende** bruge NB-IoT:

- NB-IoT er valgt med hensyn til strømforbrug og dækning i projektkonteksten
  (relateret til issue #6 om cellulær forbindelse og strømforbrug).
- LTE-M og alle kombinationsmoden (`LTE_M`, `LTE_M_GPS`, `LTE_M_NBIOT`,
  `LTE_M_NBIOT_GPS`) er dermed udeladt af choice'en og kan ikke vælges af
  modemmet som fald-back.
- `tests/nb_iot_config/` indeholder en compile-time-test, der fejler buildet,
  hvis kombinationsmoden bliver aktiveret igen.

NB-IoT-kun stiller krav til både SIM-kortet og operatøren - se
[`README.md`](README.md), afsnittet "Krav til SIM-kort og operatør".

## Relevant, men endnu ikke konfigureret

- **PSM/eDRX-indstillinger** (fx `CONFIG_LTE_LC_PSM_MODULE` og
  `CONFIG_LTE_LC_EDRX_MODULE`) hører til strømforbrugs-issues, ikke
  grundopsætningen.

## AITSM data transmission

De projektspecifikke `CONFIG_AITSM_*`-indstillinger ligger i [`Kconfig`](Kconfig)
og gør transmissionsprofilen ændrbar uden kodeændringer.

### `CONFIG_AITSM_MEASUREMENT_INTERVAL_SECONDS`

Måleintervallet. Det er begrænset til 5-15 sekunder af Kconfig, mens MQTT-
afsendelsen kan ske sjældnere, når batching er valgt. Standardværdien er 15
sekunder.

### `CONFIG_AITSM_TRANSMISSION_BATCH` og `CONFIG_AITSM_TRANSMISSION_SINGLE`

En choice, der vælger mellem batching og én måling pr. MQTT-besked. Batching er
standardprofilen; single er beregnet til test og fejlsøgning.

### `CONFIG_AITSM_BATCH_INTERVAL_SECONDS`

Maksimal tid fra første måling i en batch til batchen er klar til afsendelse.
Standardværdien er 300 sekunder.

### `CONFIG_AITSM_BATCH_MAX_SAMPLES`

Fast øvre grænse for antallet af målinger i bufferens batch. Standardværdien er
20, hvilket svarer til fem minutter ved et 15-sekunders måleinterval.

### `CONFIG_AITSM_TRANSMISSION_PAYLOAD_SIZE`

Fast maksimal payload-størrelse på 2048 bytes. Grænsen forhindrer ukontrolleret
hukommelsesforbrug og giver plads til at skifte serializer senere.
