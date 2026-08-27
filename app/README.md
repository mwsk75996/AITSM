# AITSM RGB blinky

Zephyr-app til Nordic Thingy:91 X (`thingy91x/nrf9151`). RGB-LED'en fader
glidende gennem grøn, blå og rød ved hjælp af PWM.

## Mappens filer

- `CMakeLists.txt` kobler applikationen sammen med Zephyr-buildsystemet.
- `prj.conf` aktiverer PWM-driveren.
- `include/led_fader.h` deklarerer LED-faderens offentlige entrypoint.
- `src/led_fader.c` initialiserer RGB-kanalerne og laver de tre kontinuerlige
  crossfades: grøn → blå, blå → rød og rød → grøn.
- `src/main.c` starter LED-faderen.

## Build

Fra projektroden:

```bash
source scripts/activate-ncs.sh
west build -b thingy91x/nrf9151 -d build/thingy91x_nrf9151 app
```

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
