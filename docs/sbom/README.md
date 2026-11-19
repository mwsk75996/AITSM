# SBOM for Simple-firmwaren

Software Bill of Materials (SBOM) for afleveringsversionen [`Simple/`](../../Simple/README.md),
genereret med NCS' `west ncs-sbom`. Den dækker CRA Annex I 2.1 (se
[redegørelsen](../standarder-iec62443-cra.md#21--identifikation-af-komponenter-og-sårbarheder-sbom)).

| Fil | Indhold |
| --- | --- |
| `simple-firmware.spdx` | Maskinlæsbar SBOM i SPDX 2.2 |
| `simple-firmware.html` | Samme indhold som rapport, der kan læses i en browser |

SBOM'en dækker de tre images, der ligger på enheden: applikationen (`app`),
MCUboot (`mcuboot`) og NSIB (`b0`). Kun filer, der faktisk er linket ind i
firmwaren, er med. Hver pakke står med repository og commit-hash, fx
`nrfconnect/sdk-zephyr`, `nrfconnect/sdk-nrfxlib` (`nrf_modem`),
`nrfconnect/sdk-mcuboot`, `nrfconnect/sdk-mbedtls` og `zephyrproject-rtos/nanopb`.

## Generér igen

SBOM'en skal genereres igen ved hver ny firmwareversion. Byg **uden**
MQTT-password: `mqtt_credentials.h` kommer med i SBOM'en med checksum, og en
checksum af en kort, kendt fil kan bruges til at gætte passwordet.

```bash
cd Simple
source scripts/activate-ncs.sh
unset AITSM_MQTT_PASSWORD AITSM_MQTT_USERNAME
west build -p always -b thingy91x/nrf9151/ns -d build/sbom app
west ncs-sbom -d build/sbom/app -d build/sbom/mcuboot -d build/sbom/b0 \
  --license-detectors spdx-tag,full-text,external-file,git-info \
  --optional-license-detectors "" \
  --output-spdx ../docs/sbom/simple-firmware.spdx \
  --output-html ../docs/sbom/simple-firmware.html
```

Licenser findes ud fra SPDX-tags og licenstekster i filerne. Scancode-toolkit
bruges ikke, fordi det er en stor ekstra installation.

## Begrænsninger

- Projektets egne filer i `Simple/` har ingen SPDX-licenstag og står derfor
  som `NOASSERTION`.
- SBOM'en lister komponenter, men tjekker dem ikke mod sårbarhedsdatabaser.
  Det kan gøres ved at indlæse SPDX-filen i et værktøj som fx OWASP
  Dependency-Track.
- Cloud-delen er ikke med. Dens versioner er låst i
  [`cloud/docker/compose.yml`](../../cloud/docker/compose.yml) (image-digests for
  Mosquitto og QuestDB) og [`cloud/docker/ingest/Dockerfile`](../../cloud/docker/ingest/Dockerfile)
  (Python 3.12.15 med digest og `paho-mqtt==2.1.0`).
