# AITSM Engineering ApS

IoT-projekt for **Projekt C: Nordic Thingy:91 X – cellulær kommunikation**.

## Projektet

Projektet udvikles på Nordic Thingy:91 X med nRF9151 gennem nRF Connect SDK (NCS) v3.4.0, som er baseret på Zephyr 4.4.0.

Løsningen skal indsamle data fra Thingy:91 X og sende dem sikkert til en cloud-platform, hvor de kan lagres og eventuelt visualiseres.

## Teknologistak

- **Hardware:** Nordic Thingy:91 X / nRF9151
- **Firmware:** Zephyr RTOS via nRF Connect SDK
- **Kommunikation:** TLS og MQTT
- **Dataformat:** SparkplugB
- **Cloud:** MQTT-broker og tidsseriedatabase, eventuelt med dashboard
- **Data:** `device-id`, `timestamp` og aflæste værdier

Modbus kan anvendes til dataopsamling, hvor det er relevant, men er ikke et specifikt krav for Projekt C.

## Udviklingsmiljø

Det lokale udviklingsmiljø anvender NCS v3.4.0 og Zephyr 4.4.0. Aktivér miljøet fra projektroden med:

```bash
source scripts/activate-ncs.sh
```

Board-target for Thingy:91 X med nRF9151 er:

```text
thingy91x/nrf9151
```

## Projektstruktur

- `docs/` – projektdokumentation, krav og tekniske referencer
- `scripts/` – hjælpeværktøjer til udviklingsmiljøet

Se [dokumentationsoversigten](docs/README.md) for referencefiler, PDF’er og projektets baggrundsmateriale.
