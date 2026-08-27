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
thingy91x/nrf9151/ns
```

Boardet skal bygges som **non-secure (`/ns`)**, fordi `CONFIG_NRF_MODEM_LIB`
(og dermed LTE/MQTT-funktionaliteten) kræver
`CONFIG_TRUSTED_EXECUTION_NONSECURE=y`. Den secure-variant af boardet
(`thingy91x/nrf9151` uden `/ns`) sætter ikke dette flag og kan derfor ikke
bruge modembiblioteket. Se
[`app/KCONFIG.md`](app/KCONFIG.md) for detaljer om de enkelte
Kconfig-symboler.

### `scripts/` – aktivering af værktøjerne

Filen `scripts/activate-ncs.sh` aktiverer de versioner af `west`, CMake,
Ninja, nRF Util og ARM-toolchainen, som hører til NCS v3.4.0. Scriptet skal
**sources** fra projektroden, fordi det eksporterer miljøvariabler til den
aktuelle terminal:

```bash
source scripts/activate-ncs.sh
```

Det kan kontrolleres med:

```bash
west --version
nrfutil --version
echo "$ZEPHYR_BASE"
```

Hvis scriptet køres som `bash scripts/activate-ncs.sh`, forsvinder de
eksporterede variabler igen, når scriptet afslutter. Derfor bruges `source`.

### `app/` – selve Zephyr-applikationen

`app/` er applikationens Zephyr-projekt. Det består af:

- `app/CMakeLists.txt` – fortæller Zephyr, hvordan applikationen bygges, og
  hvilke kildefiler der skal med.
- `app/prj.conf` – aktiverer GPIO, PWM, nRF-modem, LTE Link Control og
  IP-socket-offload.
- `app/include/led_status.h` og `app/src/led_status.c` – styrer RGB-LED’en
  efter forbindelsesstatus.
- `app/include/network.h` og `app/src/network.c` – initialiserer modemmet,
  starter LTE-forbindelsen og håndterer registrerings-events.
- `app/src/main.c` – applikationens entrypoint.
- `app/README.md` – vejledning til build, flash og LED-status.

### Build

Fra projektroden aktiveres miljøet først, og derefter bygges `app/` til
board-targetet:

```bash
source scripts/activate-ncs.sh
west build -b thingy91x/nrf9151/ns -d build/thingy91x_nrf9151 app
```

Build-outputtet ligger i `build/thingy91x_nrf9151/`. Mappen er ignoreret af
Git, fordi den kun indeholder genererede filer. Den vigtigste firmwarepakke
til USB/MCUboot-upload er:

```text
build/thingy91x_nrf9151/dfu_application.zip
```

### Flash til Thingy:91 X

Kontrollér først, at boardet er tændt og tilsluttet med et USB-datakabel:

```bash
source scripts/activate-ncs.sh
nrfutil device list
```

Find boardets id i outputtet, og brug det ved upload. På Thingy:91 X fungerer
USB/MCUboot-metoden med den genererede ZIP-fil:

```bash
nrfutil device program \
  --firmware build/thingy91x_nrf9151/dfu_application.zip \
  --serial-number THINGY91X_XXXXXXXXXXXX \
  --traits mcuBoot \
  --family nrf91 \
  --options target=nRF91,mcu_end_state=NRFDL_MCU_STATE_APPLICATION
```

Erstat `THINGY91X_XXXXXXXXXXXX` med det faktiske device-id fra `nrfutil
device list`. En succesfuld upload afsluttes uden fejl, og boardet resettes
til den nye applikation.

På Linux kan en fejl med `errno 13` skyldes manglende Nordic-udev-regler.
Reglerne installeres én gang med:

```bash
pkexec sh -c 'install -m 644 /home/matt/ncs/toolchains/fbf7391cab/nrfutil/home/share/nrfutil-device/udev/rules.d/99-mm-nrf-blacklist.rules /etc/udev/rules.d/99-mm-nrf-blacklist.rules; install -m 644 /home/matt/ncs/toolchains/fbf7391cab/nrfutil/home/share/nrfutil-device/udev/rules.d/71-nrf.rules /etc/udev/rules.d/71-nrf.rules; udevadm control --reload-rules; udevadm trigger'
pkexec udevadm trigger --action=add --subsystem-match=tty
pkexec udevadm trigger --action=add --subsystem-match=usb
```

Advarslen om manglende `JLinkARM DLL` er ikke afgørende for USB/MCUboot-
metoden ovenfor. Den er relevant, hvis der senere flashes via en fysisk
SEGGER J-Link-probe.

## Projektstruktur

- `docs/` – projektdokumentation, krav og tekniske referencer
- `scripts/` – hjælpeværktøjer til udviklingsmiljøet
- `app/` – Zephyr-applikation til NB-IoT-forbindelse på Thingy:91 X
- `tests/` – automatiserede firmware-tests (køres med Twister)
- `cloud/visualization/` – visualization-websitet og dets API
- `build/` – lokale, genererede build-filer (ignoreres af Git)

Se [dokumentationsoversigten](docs/README.md) for referencefiler, PDF’er og projektets baggrundsmateriale.

Lavet af Thomas kun Thomas
