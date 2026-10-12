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

Modbus RTU/TCP-dataopsamling skal også indgå i Projekt C og følges i #93.

## Versioner

Repositoryet indeholder to firmwareversioner, der bruger samme cloud og samme
dokumentation:

| Mappe | Formål |
| --- | --- |
| [`Original/`](Original/README.md) | Den fulde firmware med tests og måleværktøjer. Fungerer som laboratorium. |
| [`Simple/`](Simple/README.md) | Den forenklede afleveringsversion, der kun indeholder det, projektkravene kræver. |

Hver version har sin egen `app/`, `tests/` og `scripts/`. Firmwarekommandoer
køres fra versionens mappe, fx `cd Original`, så buildoutput og testoutput
lægges i versionens egen ignorerede `build/` og `twister-out/`.

## Udviklingsmiljø

Begge versioner anvender NCS v3.4.0 og Zephyr 4.4.0. Aktivér miljøet fra
versionens mappe med:

```bash
cd Original
source scripts/activate-ncs.sh
```

Board-target for Thingy:91 X med nRF9151 er:

```text
thingy91x/nrf9151/ns
```

Boardet skal bygges som **non-secure (`/ns`)**, fordi `CONFIG_NRF_MODEM_LIB`
(og dermed LTE/MQTT-funktionaliteten) kræver
`CONFIG_TRUSTED_EXECUTION_NONSECURE=y`. Den secure-variant af boardet
(`thingy91x/nrf9151` uden `/ns`) sætter ikke dette flag og kan derfor ikke
bruge modembiblioteket. Se
[`Original/app/KCONFIG.md`](Original/app/KCONFIG.md) for detaljer om de enkelte
Kconfig-symboler.

Build, flash og serielt debug-output er beskrevet i
[`Original/README.md`](Original/README.md).

## Devicetree og memory layout

Firmware bruger Nordic's standard-devicetree for `thingy91x/nrf9151/ns`. Der er
ingen projektspecifik `.overlay` til GPIO eller andre board-ændringer; RGB-LED
og øvrige standardfunktioner hentes fra boardets egen devicetree.

Memory layout ændres heller ikke manuelt. Det håndteres af NCS' standard
Partition Manager samt MCUboot-overlays, som genereres under buildet.

## Projektstruktur

- `Original/` – fuld firmware (`app/`), firmware-tests (`tests/`) og scripts (`scripts/`)
- `Simple/` – forenklet afleveringsversion med samme opdeling
- `cloud/` – fælles MQTT-broker, ingest og visualisering
- `docs/` – fælles projektdokumentation, krav og tekniske referencer
- `.github/` – fælles GitHub Actions-workflows
- `project_context/` – lokal konfiguration; credentials holdes uden for Git

Se [dokumentationsoversigten](docs/README.md) for referencefiler, PDF’er og projektets baggrundsmateriale.
