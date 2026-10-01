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
- `Firmware (Twister)`: `data_transmission` (batch og single) samt `led_status` på `native_sim/native/64`, med Nordic-toolchainen til NCS v3.4.0 og Zephyr fra `ncs-v3.4.0`.

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
