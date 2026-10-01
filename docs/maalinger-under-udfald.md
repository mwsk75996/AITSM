# Målinger under MQTT-udfald (#77)

## Beslutning

Mulighed A: fortsæt lokal indsamling på det konfigurerede måleinterval, også
uden MQTT/LTE, og send bufferede målinger samlet ved reconnect. Strøm og data
spares ved at adskille måletimeren fra afsendelse: offline sampling udløser
ingen MQTT-kald, og reconnect/ACK flytter ikke måletidspunkterne. Der bruges
fortsat batch som standard; der tilføjes ingen netværksfelter eller flashlog.

Den faste buffer beholder de ældste ubekræftede data. Når den er fuld, springes
nye målepladser over; gamle data overskrives aldrig. Der tælles tabte pladser,
og sensor-/modemlæsninger springes over, når resultatet ikke kan gemmes.
Det fulde-buffer-forløb logges samlet for at begrænse logarbejdet. Efter ACK
frigives kun den kvitterede del, og sampling fortsætter på sin eksisterende timer.

Standard: 20 pladser ved 15 s (ca. fem minutters målepladser i en tom buffer).
Pladsen til et udfald er `(20 - allerede bufferede målinger) × 15 s`.
Single har én plads. Kconfig-grænser og worst-case payload er uændrede; #79
behandler ugyldige buffer/payload-kombinationer. Sensorfejl og ugyldig UTC-tid
kan også give huller. RAM-data og tabstæller overlever ikke reboot/strømsvigt.

Dette er en afgrænset garanti: målinger inden for ledig kapacitet bevares til
broker-PUBACK. Det er ikke ubegrænset retention eller kvittering for QuestDB;
#81 behandler ingest-udfald. Strømbesparelsen er en designbeslutning, ikke en
målt procent; PSM/eDRX og målt baseline hører til #23 og #32.

## Implementering

`measurement_source.c` samler hardwarelæsningerne. `measurement_service.c`
planlægger sampling og transmission uafhængigt på systemworkqueue. Sampling
starter efter modeminitialisering; MQTT-status styrer kun afsendelse. Under
PUBACK-ventetid kan nye målinger føjes til bufferen, hvis der er plads. ACK
committer kun det oprindelige antal, så de nye data ikke bliver fjernet.

Fejlet publish genforsøges med måleintervallet som ventetid, og nye målinger
fremskynder ikke en eksisterende retry. MQTT-tab annullerer publish-retry og
beholder målebufferen. Reconnect dræner den straks. En tom buffer beder nu
ikke fejlagtigt om flush, selv om den aktuelle tid er over batchintervallet.

## Validering

Native tests af måleservice og buffer kører både batch og single. De dækker
offline-sampling, genoprettet forbindelse, bevarede data ved afbrud, fuld
buffer/tabstæller uden sensorlæsninger, forsinket PUBACK og de nye målinger
under ventetiden, fejlede publishes og annullering af retries. Desuden testes
ugyldig tid/sensorfejl og at en tom buffer ikke kræver flush.

Hardware- og CI-resultater tilføjes før RTM.
