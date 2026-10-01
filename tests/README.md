# Tests

Automatiserede tests til AITSM-firmwaren. Testsne køres med Twister fra
projektroden:

```bash
source scripts/activate-ncs.sh
west twister -p thingy91x/nrf9151/ns -T tests
```

## CI

GitHub Actions-workflowet [`Tests`](../.github/workflows/tests.yml) kører automatisk på PR'er mod `main` og push til `main`. Det kan også startes manuelt:

- `MQTT-ingest (pytest)`: ingest-tests med Python 3.12.
- `Firmware (Twister)`: `data_transmission` (batch og single), `led_status`, `mqtt_client`, `mqtt_reconnect`, `app_controller` og `measurement_service` på `native_sim/native/64`, med Nordic-toolchainen til NCS v3.4.0 og Zephyr fra `ncs-v3.4.0`.

Testresultater og logs gemmes som Actions-artifacts, også hvis tests fejler. Workflowet kræver ingen credentials, broker, QuestDB eller hardware. `nb_iot_config` er en separat build-only-test til Thingy-boardet og køres manuelt med kommandoen ovenfor.

## `nb_iot_config/`

Compile-time-test der sikrer, at systemet er konfigureret til udelukkende at
bruge NB-IoT (issue #25). Testen bygger hele applikationen til
`thingy91x/nrf9151/ns` med applikationens egne `app/prj.conf` og fejler, hvis
en af følgende bryder:

- `CONFIG_LTE_NETWORK_MODE_NBIOT` er ikke slået til.
- LTE-M, kombinationsmoden (`LTE_M_NBIOT`, `LTE_M_NBIOT_GPS`) eller NTN NB-IoT
  er slået til.

Asserts står i [`nb_iot_config/src/config_asserts.c`](nb_iot_config/src/config_asserts.c).

## `data_transmission/`

Native Zephyr-test, der verificerer den faste målebuffer, timestamp-formatet,
batch-flush efter standardprofilens fem minutter og single-mode. Testen bruger
samme `data_transmission.c` som firmware-buildet og kan køres uden Thingy:

```bash
west twister -p native_sim -T tests/data_transmission
```

Payload-grænsen (issue #12) er dækket af tre tests:

- En for lille payloadbuffer giver `-EMSGSIZE`, og målingerne bliver i bufferen.
- En fuld buffer afviser nye målinger (`-ENOSPC`/`-EBUSY`) og beder om flush.
- En fuld buffer med de længst mulige værdier kan formateres inden for
  `CONFIG_AITSM_TRANSMISSION_PAYLOAD_SIZE`. Testen fejler, hvis Kconfig
  kombineres, så en fuld batch aldrig kan sendes, f.eks.
  `-x CONFIG_AITSM_BATCH_MAX_SAMPLES=64`.

Testen kan også køres som `native_sim/native/64`.

## `led_status/`

Native Zephyr-test uden hardware, der verificerer farve- og blinkmønstrene for
hver LED-status samt at et midlertidigt publish-blink (succes eller fejl) vender
tilbage til den seneste stabile forbindelsesstatus. Testen bruger samme
`led_status.c` som firmware-buildet; PWM-delen er guardet med `DT_HAS_ALIAS`, så
modulet kan bygges til `native_sim` uden en PWM-controller.

```bash
west twister -p native_sim -T tests/led_status
```

På værter uden 32-bit host-headere kan den 64-bit variant bruges i stedet:

```bash
west twister -p native_sim/native/64 -T tests/led_status
```

## `mqtt_client/`

Native tests af den rigtige `mqtt_client.c` med en kontrollerbar fake af NCS MQTT-helperen. Tester at systemworkqueue fortsat kører, mens connect eller publish blokerer, at payloaden kopieres og ikke kan overskrives af et nyt publish, og at afsendelsesfejl og PUBACK rapporteres som app-events. Tester også afvisning uden LTE, annullering af køarbejde ved LTE-tab, dublerede connect-forsøg, sene CONNACK/fejl og gamle/dublerede PUBACK. Testcredentials er kun dummyværdier til fake-backenden.

```bash
west twister -p native_sim/native/64 -T tests/mqtt_client
```

## `mqtt_reconnect/` og `app_controller/`

`mqtt_reconnect` tester den rigtige tilstandsmaskine med standardindstillinger
(5–60 s) og et andet interval (3–19 s): fordobling og loft, nulstilling,
annullering ved LTE-tab samt dublerede events. `app_controller` tester den
rigtige controller med fake MQTT-, LED- og måleservice-API'er og rigtige
Zephyr-workqueue-timere (1–2 s). Den verificerer genforsøg uden et nyt LTE-event,
uændret deadline ved ERROR + DISCONNECTED, stop af målinger ved LTE-tab,
afvisning af sen CONNACK og genforsøg efter køafvisning.

```bash
west twister -p native_sim/native/64 -T tests/mqtt_reconnect -T tests/app_controller
```

## `measurement_service/`

Native tests af den rigtige måleservice og RAM-buffer med fake sensor- og
MQTT-API'er og rigtige Zephyr-timere. Kører både batch og single. Verificerer
målinger uden MQTT, dræn ved reconnect, fuld buffer uden ubrugelige
sensorlæsninger, tabstæller, fortsat indsamling under afbrud, kvittering af
kun den sendte del og afgrænsede publish-genforsøg. Sensorfejl/ugyldig tid
må ikke lægge en måling i buffer. Hardwarelæsningerne ligger særskilt i
`measurement_source.c`, som medtages i det rigtige Thingy-build.

```bash
west twister -p native_sim/native/64 -T tests/measurement_service
```

## `cloud/mqtt/tests/`

Pytest-tests af MQTT-til-QuestDB-broen `cloud/mqtt/ingest.py`: timestamp-
parsing, validering af værdier, line protocol-escaping, single- og
batchpayloads i præcis det format firmwaren sender, samt at ugyldige beskeder
og QuestDB-fejl afvises uden at servicen crasher. Testene kræver hverken broker
eller QuestDB:

```bash
cd cloud/mqtt
python3 -m venv .venv
.venv/bin/pip install -r requirements-dev.txt
.venv/bin/python -m pytest
```
