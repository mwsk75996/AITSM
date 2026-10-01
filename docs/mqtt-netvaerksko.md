# MQTT-netværkskø (#75)

## Ændring

Connect og publish kører på en separat Zephyr-workqueue med egen stack (4096 bytes) og preemptiv prioritet 5. Målinger og app-events bliver på systemworkqueue. MQTT-workeren overtager en kopi af payloaden og returnerer fejl og PUBACK som app-events; kun PUBACK med succes fjerner målinger fra bufferen.

`mqtt_helper_publish()` blev også flyttet, fordi den kalder Zephyrs synkrone socket-skrivning under klientens mutex. En ekstra payload afvises med `-EBUSY`, mens en skrivning er ventende eller i gang.

## Automatiseret validering

Native Twister: 20/20 testcases i fire konfigurationer bestået, inklusive seks nye tests af den rigtige `mqtt_client.c` med fake MQTT-helper. De nye tests blokerer connect/publish med semaforer og verificerer samtidig, at en probe på systemworkqueue bliver behandlet. Desuden testes payloadkopiering, afvisning af overskrivning, fejl-events og PUBACK. CI kører suiten sammen med de eksisterende tests; pytest består med 51/51 tests.

## Hardwaretest, 1. oktober 2026

- Enhed: Thingy:91 X, `THINGY91X_0ABA4D24A1A`, modemfirmware `mfw_nrf91x1_2.0.2`.
- Applikationskode: `2925dea`, NCS v3.4.0 / Zephyr 4.4.0.
- Testprofil: single, måling hvert 15. sekund, AT-shell-overlay og lokal thread-monitor-konfiguration. Standardprofilen i repository’et er stadig batch.
- Et rent firmware-build og USB/MCUboot-flash bestod.
- Connect startede ved uptime `20.555114 s`. Under connect blev LTE deaktiveret med `AT+CFUN=4`.
- `LTE-afbrudt event behandlet` blev logget fra app-controllerens systemworkqueue ved `22.235565 s`, før det blokerende connect returnerede `-116` ved `22.235687 s`. App-events blev altså behandlet, mens netværkskaldet endnu var i gang. LED-farven er ikke aflæst visuelt.
- Efter `AT+CFUN=1` blev LTE registreret igen ved `30.129943 s`; næste connect tog 6292 ms og MQTT-CONNACK blev behandlet ved `36.690185 s`.
- En måling blev lagt i publish-kø ved `36.695373 s`, fik PUBACK ved `37.004119 s` og blev derefter fjernet fra bufferen.
- `AT+CFUN?` returnerede `+CFUN: 1`. Test-overlayet deaktiverer nu shell-wildcards, som ellers opslugte spørgsmålstegnet og viste shell-hjælp.

Rå seriallogs og lokale testkonfigurationer ligger kun i den ignorerede `build/`-mappe. MQTT-reconnect uden LTE-tab behandles i #76.
