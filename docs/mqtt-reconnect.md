# MQTT-reconnect (#76)

## Adfærd

Controlleren starter straks connect ved LTE-registrering. Ved connect-fejl
eller MQTT-disconnect genforsøger den efter 5, 10, 20, 40 og højst 60 sekunder,
så længe LTE er oppe. En accepteret CONNACK nulstiller backoff. LTE-tab
annullerer retry-timeren og stopper afsendelse; en ny LTE-registrering
starter et forsøg med det samme. Alle netværkskald udføres på køen fra #75.

ERROR og DISCONNECTED fra samme forsøg planlægger kun én retry og flytter
ikke deadlinen. Klienten afviser overlappende connect-forsøg og operationer
uden LTE, ignorerer PUBACK fra tidligere publish/sessioner og lukker en sen
CONNACK efter LTE-tab. Arbejde, som stadig ligger i kø ved LTE-tab, kører ikke
mod helperen. Et DNS/TCP/TLS-kald, som allerede kører, afsluttes først, når
helperen returnerer; det blokerer fortsat ikke systemworkqueue.

## Automatiseret validering

CI på applikationscommit `45536c3` (PR-head `c47ae01`) bestod:

- 44/44 native testcases i syv konfigurationer.
- 51/51 pytest-tests af ingest.
- Thingy:91 X-build med testprofil og et separat build med standardprofil bestod lokalt.
- `actionlint` og `git diff --check` bestod.

Backoff testes med 5–60 s og 3–19 s. Controllerens tests bruger rigtige
Zephyr-workqueue-timere (1–2 s) og verificerer reconnect uden nyt LTE-event,
annullering ved LTE-tab, måleservicens start/stop, dublerede fejl-events og
sen CONNACK. MQTT-klientens tests verificerer desuden køannullering,
parallelle connect-forsøg, gamle/dublerede PUBACK og fejl fra en tidligere
session. De oprindelige tests af systemworkqueue under blokerende connect
og publish består fortsat.

## Hardwaretest, 1. oktober 2026

Thingy:91 X `THINGY91X_0ABA4D24A1A`, modemfirmware `mfw_nrf91x1_2.0.2`,
NCS v3.4.0 / Zephyr 4.4.0. Testprofil: batch med fire målinger, måleinterval
5 s, batch-timeout 30 s og AT-shell-overlay. Firmware blev flashet via
USB/MCUboot. Standardprofilen er fortsat 15 s, 20 målinger og 300 s.

To `systemctl restart mosquitto` blev udført på VPS'en. LTE forblev
registreret under begge genstarter; intet LTE-event eller reboot blev brugt
til at gendanne MQTT.

| Hændelse | Første genstart, uptime | Anden genstart, uptime |
| --- | --- | --- |
| MQTT-disconnect behandlet | 46.232910 s | 59.608215 s |
| Reconnect starter | 51.233093 s | 64.608428 s |
| Accepteret CONNACK | 57.555480 s | 71.324707 s |
| Batch med fire målinger kvitteret | 58.129516 s | 87.566986 s |

Begge genforsøg startede efter fem sekunder. MQTT kom op ca. 11,3 og 11,7 s
efter det registrerede brud, inklusive TLS. Anden genstart brugte igen fem
sekunder, hvilket viser nulstilling efter succes.

Ved første genstart blev en batch med fire målinger lagt i publish-kø,
mens den gamle socket stadig så forbundet ud. Forbindelsesbruddet kom før
PUBACK. Alle fire målinger blev bevaret, gensendt efter reconnect og først
fjernet efter PUBACK fra den nye session.

Efter anden batch blev LTE slukket med `AT+CFUN=4`. Controlleren behandlede
LTE-tab ved 89.110412 s; to efterfølgende MQTT-disconnect-events udløste
ingen reconnect. Efter `AT+CFUN=1` blev LTE registreret ved 102.672119 s,
og connect startede straks ved 102.672332 s. CONNACK kom ved 108.685333 s,
og endnu en batch blev kvitteret ved 124.577636 s. `AT+CFUN?` returnerede 1.

QuestDB indeholdt alle 12 målinger fra de tre kvitterede batches i perioden
08:06:01–08:07:34 UTC med værdierne fra enhedsloggen. Både `mosquitto` og
`projekt-c-ingest` var aktive efter testen. LED-status er verificeret med
unit tests og eventrækkefølge; farverne er ikke aflæst visuelt.

Efter testen blev standardfirmwaren uden AT-overlay flashet tilbage.
Bootloggen bekræftede batchprofil med 15 s, MQTT-forbindelse ved 30.676 s
og en ny indsamlet måling ved 30.681 s.

Rå seriallogs, testoverlay og databaseudtræk findes kun lokalt i den
ignorerede `build/`-mappe. Testcase 3 i #12 består nu.

## Resterende fund

Efter #77 fortsætter måleservicen under MQTT/LTE-udfald med afgrænset RAM-buffer;
se [beslutning og validering](maalinger-under-udfald.md). PUBACK bekræfter
brokerens modtagelse, ikke lagring i QuestDB; ingest-udfald kan derfor stadig
tabe data (#81). LTE-aware LED-status og bevaret fejlstatus forbedrer #82,
som fortsat kræver den samlede accepttest. Dublet- og payloadgrænseproblemerne
følges separat i #78 og #79.
