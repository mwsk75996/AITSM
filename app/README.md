# AITSM cellular connection

Zephyr-app til Nordic Thingy:91 X (`thingy91x/nrf9151/ns`). Applikationen
initialiserer nRF9151-modemet og etablerer en LTE-forbindelse via SIM-kortet.
LTE-M og NB-IoT er aktiveret, så modemmet kan vælge en understøttet teknologi.

RGB-LED'en viser forbindelsesstatus:

- Blå: søger efter LTE-netværk eller forbinder.
- Grøn: LTE-M-forbindelse er aktiv.
- Gul: NB-IoT-forbindelse er aktiv.
- Rød: ingen forbindelse eller fejl.

## Mappens filer

- `CMakeLists.txt` kobler applikationen sammen med Zephyr-buildsystemet.
- `prj.conf` aktiverer PWM, nRF-modem, LTE Link Control og IP-socket-offload.
- `include/led_status.h` deklarerer LED-statusmodulets API.
- `include/network.h` deklarerer netværksmodulets API.
- `src/led_status.c` styrer RGB-LED'en.
- `src/network.c` initialiserer modemmet, starter LTE-forbindelsen og reagerer på
  ændringer i netværksregistreringen.
- `src/main.c` initialiserer LED- og netværksmodulerne.

## Build

Fra projektroden:

```bash
source scripts/activate-ncs.sh
west build -b thingy91x/nrf9151/ns -d build/thingy91x_nrf9151 app
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

Åbn en seriel terminal efter flash. Ved en vellykket forbindelse vises
`LTE registered ...` og `LTE mode: LTE-M` eller `LTE mode: NB-IoT` i loggen.
LED'en lyser grøn ved LTE-M og gul ved NB-IoT. Før registrering lyser LED'en
blå; ved mistet forbindelse bliver den rød, mens modemmet forsøger at
genoprette forbindelsen.
