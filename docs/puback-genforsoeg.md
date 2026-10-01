# Afgrænsede genforsøg ved manglende PUBACK (#87)

## Leveringsforløb

Et accepteret publish får et lokalt 32-bit token. MQTT-workeren kopierer
payloaden og tildeler et MQTT message-id, før socket-skrivningen starter.
Der er højst én afsendelse i gang eller afventende PUBACK. Den kopierede
payload bliver liggende uændret, også når måleservicen indsamler nye data.

Efter en vellykket socket-skrivning starter PUBACK-timeren. Med standarderne
venter den 30 s, genudsender den oprindelige payload med samme message-id og
`DUP=1`, og venter derefter 60, 120, 240 og højst 300 s. Genforsøg fortsætter
med dette loft, indtil ACK eller forbindelsestab; der oprettes ingen ekstra
TLS-forbindelse, og nye målinger flytter ikke deadline. Socket-skrivefejl
rapporteres som før til måleservicens afgrænsede retry-flow.

Nordics helper bruger MQTT 3.1.1. Den tillader genlevering af ubekræftede
beskeder på den eksisterende forbindelse; se [OASIS, afsnit 4.4](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/mqtt-v3.1.1.html).
Dette valg skal genvurderes ved skift til MQTT v5, som begrænser genlevering
til reconnect. Payloadens oprindelige tidsstempler bevares, så #78 fjerner
mulige dubletrækker i QuestDB.

## Kvitteringer og ejerskab

En ACK til et tidligere genforsøg af **samme** afsendelse er gyldig: alle
forsøg har samme payload/message-id og samme lokale token. Kun denne
oprindelige del af målebufferen fjernes. Nyere målinger fra ventetiden
bliver liggende og sendes med deres eget message-id og token.

ACK/disconnect og planlægning af næste timeout beskyttes sammen, så en
forsinket callback ikke kan annullere en nyere afsendelses timer. ACK kan
også ankomme, før socket-skrivningen returnerer; payloaden kan først
overskrives, når skrivningen er helt færdig. Et allerede påbegyndt retry kan
nå at sende én gentagelse samtidig med ACK; deduplikering håndterer denne.

Completion-events indeholder det lokale token hele vejen gennem app-køen.
Måleservicen afviser et resultat fra en tidligere afsendelse/session, også
hvis det først behandles efter reconnect. Controlleren ændrer heller ikke
LED-status for et sådant resultat. Tokenet tilføjes ikke til MQTT-payloaden.

Ved MQTT-/LTE-tab annulleres timeren; ubekræftede målinger bevares til
reconnect. Normal ACK annullerer timeren uden yderligere trafik. RAM,
bufferkapacitet og sampling er uændrede. Fuld buffer kan stadig koste nye
målepladser, og reboot/strømsvigt mister RAM-indhold som beskrevet i #77.

## Konfiguration og validering

- `CONFIG_AITSM_MQTT_PUBACK_TIMEOUT_SECONDS`: standard 30, interval 1–3600.
- `CONFIG_AITSM_MQTT_PUBACK_MAX_DELAY_SECONDS`: standard 300, interval 1–3600.
- Buildet afviser initial ventetid større end loftet.

Native-tests bruger rigtige Zephyr-timere med 1 s initialt og 4 s loft.
De undertrykker PUBACK, holder MQTT/LTE oppe og kræver identisk payload,
message-id og DUP-flag ved retries med 1/2/4/4 s. ACK skal stoppe genforsøg,
og en ny afsendelse skal kunne starte på samme forbindelse. Der testes også
retry-skrivefejl, LTE-tab, gammel ACK efter et nyere publish, ACK under en
igangværende skrivning og ACK inden socket-return.

Måleservice-tests i både batch og single kontrollerer, at en gammel
completion ikke fjerner data fra en nyere afsendelse, samt at den sendte
prefix fjernes uden at røre nye målinger. Den øvrige suite dækker fuld buffer,
fortsat sampling og uændrede retry-deadlines. Der er 72 native testcases i
ni konfigurationer; den reviderede timer-/callback-lås er desuden verificeret
med alle 21 MQTT-klienttests. Manglende/forsinket ACK testes med kontrolleret
fake-backend, ikke ved at ændre den kørende broker.

## Ingest-deploy fra #81

Første live-deploy af #81 afslørede, at den eksisterende GitHub deploy-konto
kun må køre Nginx-helperen og ikke kan skrive ingest-mappen. Ingen services
blev ændret ved det afviste deploy. En afgrænset root-ejet ingest-helper er
forberedt i [cloud/mqtt/deploy](../cloud/mqtt/deploy/README.md); installation
kræver en engangsændring af VPS-administratoren. SSH og host key håndteres
fortsat kun via GitHub Secrets. Live-udfaldstesten må afvente denne adgang.

Det separate #88 følger tab ved fejlet QuestDB-skrivning op. PSM/eDRX (#23)
og målt strøm-/dataforbrug (#32) er fortsat senere opgaver.
