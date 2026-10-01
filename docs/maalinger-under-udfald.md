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

CI på applikationscommit `b83e3aa`: 62/62 native testcases i ni
konfigurationer og 58/58 pytest-unit-tests bestod. Den isolerede databaseintegration
fra #78 bestod med 59 Python-tests inklusive rigtig QuestDB. Thingy-builds med
test- og standardprofil, `actionlint` og diff-check bestod.

## Hardwaretest, 1. oktober 2026

Thingy:91 X `THINGY91X_0ABA4D24A1A`, modemfirmware `mfw_nrf91x1_2.0.2`,
NCS v3.4.0 / Zephyr 4.4.0. Lokal testprofil: batch, 5 s, 12 pladser og 60 s
batch-timeout med AT-shell. Eksisterende batches blev kvitteret før flash og
før tilbageførsel til standardprofilen.

MQTT-udfald blev fremprovokeret ved at åbne en kort, autentificeret MQTT-session
med enhedens client-id. Brokeren lukkede den tidligere socket, og enheden fik
samme DISCONNECTED-event som ved broker-genstart. LTE, broker og ingest var
aktive gennem denne variant af testcase 3 i #12. En rigtig broker-genstart
med reconnect er tidligere verificeret i [#76-rapporten](mqtt-reconnect.md).
LTE-udfald (testcase 2) blev derefter testet med `AT+CFUN=4/1`.

| Hændelse | Uptime |
| --- | --- |
| Første gyldige måling, før MQTT-forbindelse | 26.045196 s |
| Første MQTT-CONNACK | 27.926940 s |
| MQTT-disconnect behandlet, LTE oppe | 37.687438 s |
| Reconnect starter | 42.687622 s |
| Reconnect-CONNACK | 66.293609 s |
| Otte bufferede målinger kvitteret | 67.067596 s |
| LTE-tab behandlet | 78.518157 s |
| LTE registreret igen | 106.489196 s |
| MQTT-CONNACK efter LTE-udfald | 114.137390 s |
| Ni bufferede målinger kvitteret | 117.242462 s |
| Tolv yderligere målinger kvitteret | 172.735412 s |

- Seks målinger blev indsamlet, mens kun MQTT var nede. Connect-kaldet tog
  23.324 s, og sampling fortsatte under kaldet.
- Seks målinger blev indsamlet, mens LTE var nede, også med `CFUN=4`.
- En ny måling ved 115.989196 s kom efter kopiering af payloaden med ni
  målinger, men før dens PUBACK. Den blev bevaret og kom med i næste batch.
- Alle **30 målinger** fra den serielle log blev fundet i QuestDB gennem
  read-only-web-API'et, i samme rækkefølge og med identiske temperatur- og
  batteriværdier. Tidsvindue: 09:03:56–09:06:21 UTC, fem sekunder mellem
  alle rækker, ingen huller eller dubletter. Ingen buffer-overløb i denne test.
- Fuld buffer, tabstæller og fravær af ubrugelige sensorlæsninger er dækket
  af native tests i både batch og single.
- `AT+CFUN?` returnerede 1 efter testen.

Standardfirmware uden AT-overlay blev flashet tilbage: batch/15 s, 20 pladser,
300 s. Bootloggen viste MQTT-forbindelse ved 30.492340 s og en måling ved
31.381805 s. Rå logs og databaseudtræk ligger kun i ignoreret `build/`.

## Opfølgning

#81 implementerer den vedvarende ingest-session; live-deploy og udfaldstest
afventer den [afgrænsede deploy-adgang](../cloud/mqtt/deploy/README.md).
Mistet PUBACK håndteres nu af de [afgrænsede genforsøg i #87](puback-genforsoeg.md).
#88 følger fejlet HTTP-skrivning til QuestDB op. #23/#32 skal verificere PSM/eDRX og det faktiske strøm-/dataforbrug.
