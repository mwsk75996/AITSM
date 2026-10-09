# AITSM Original

Original er den fulde firmwareversion til Nordic Thingy:91 X og fungerer som
projektets laboratorium. Her afprøves robusthed, transmissionsprofiler og
målinger, før det besluttes, hvad der tages med i [Simple](../Simple/README.md).

Cloud og dokumentation er fælles for begge versioner og ligger i projektroden:
[`cloud/`](../cloud/) og [`docs/`](../docs/README.md).

## Mappens indhold

- `app/` – Zephyr-applikationen. Se [`app/README.md`](app/README.md) for filer,
  trådstruktur, dataprofil, SparkplugB, strømbesparelse, build og flash.
- `tests/` – automatiserede firmware-tests, der køres med Twister. Se
  [`tests/README.md`](tests/README.md).
- `scripts/` – hjælpeværktøjer til udviklingsmiljøet og målinger.

Alle kommandoer herunder og i undermappernes README'er køres fra `Original/`:

```bash
cd Original
```

## `scripts/` – aktivering af værktøjerne

Filen `scripts/activate-ncs.sh` aktiverer de versioner af `west`, CMake,
Ninja, nRF Util og ARM-toolchainen, som hører til NCS v3.4.0. Scriptet skal
**sources**, fordi det eksporterer miljøvariabler til den aktuelle terminal:

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

Til at følge enhedens serielle debug-output findes `scripts/monitor-serial.py`.
Det finder selv Thingy:91 X' konsolport (den laveste USB CDC-interface) via
`/dev/serial/by-id` og streamer loggen ved 115200 8N1:

```bash
scripts/monitor-serial.py            # auto-detektér og stream
scripts/monitor-serial.py --list     # vis fundne porte
scripts/monitor-serial.py -p /dev/ttyACM0 -b 115200
```

Scriptet kræver `pyserial` (`pip install pyserial` eller
`apt install python3-serial`). Har man ikke det, kan porten findes med
`nrfutil device list` og læses manuelt med `stty`/`cat`.

`scripts/measure-data-usage.py` og profilerne i `scripts/measurement-profiles/`
bruges til målingerne af dataforbrug og leveringstid i
[`docs/dataforbrug.md`](../docs/dataforbrug.md).

## Build

Aktivér miljøet, og byg `app/` til board-targetet:

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

## Flash til Thingy:91 X

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
