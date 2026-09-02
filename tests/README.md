# Tests

Automatiserede tests til AITSM-firmwaren. Testsne køres med Twister fra
projektroden:

```bash
source scripts/activate-ncs.sh
west twister -p thingy91x/nrf9151/ns -T tests
```

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
