# MQTT til QuestDB

`ingest.py` modtager single- og batch-JSON fra MQTT med QoS 1 og skriver
`temperature` og `battery` til `sensor_readings` over ILP/HTTP. Credentials
kommer fra servicens miljø; de må ikke gemmes i repository'et.

## Deduplikering (#78)

En måling identificeres af `(timestamp, device_id)`. Firmware sender den
oprindelige måletid ved retransmission, så den samme måling kan modtages
flere gange uden flere rækker. QuestDB-tabellen skal være WAL-aktiveret med
`timestamp` som designated timestamp og følgende nøgler:

```sql
ALTER TABLE sensor_readings DEDUP ENABLE UPSERT KEYS(timestamp, device_id);
```

Migrationen ligger i `migrations/001_enable_dedup.sql`. På VPS'en kan den
anvendes og kontrolleres med:

```bash
python3 questdb_schema.py
```

Scriptet kontrollerer schema og historiske dubletter før ændringen og venter
på, at QuestDB har anvendt begge nøgler. Det er sikkert at køre igen. Det
sletter eller genopbygger ingen historiske rækker. Eksisterende dubletter
stopper migrationen og kræver særskilt datagennemgang: DEDUP gælder nye
inserts og rydder ikke automatisk op i historiske data.

Samme tidspunkt på forskellige enheder giver separate rækker. En ny værdi
med samme enhed og måletid erstatter den tidligere værdi (last write wins).
Tidsopløsningen er ét sekund i firmwaren; flere forskellige målinger på samme
enhed skal derfor have forskellige tidsstempler. Payloads uden timestamp
får modtagelsestid og har ikke samme sikkerhed mod gentagelser.

Løsningen tilføjer ingen MQTT-felter, ekstra radioopkoblinger eller lokale
lister over allerede modtagne ID'er. Deduplikering sker ved lagringen og
virker også efter ingest-genstart. PUBACK er fortsat kun brokerens kvittering:
vedvarende ingest-session beskytter levering under ingest-udfald som beskrevet nedenfor.

Se QuestDBs primære dokumentation for [DDL og eksisterende data](https://questdb.com/docs/query/sql/alter-table-enable-deduplication/)
og [nøgler og semantik](https://questdb.com/docs/concepts/deduplication/).

## Automatiseret anvendelse og verifikation

Workflowet [`QuestDB deduplikering`](../../.github/workflows/questdb-dedup.yml)
tester mod en isoleret QuestDB 10.0.1 med fast image-digest på relevante PR'er.
Efter merge af schemafilerne til `main` køres samme test, og migrationen
anvendes på VPS'en via de eksisterende SSH-secrets og kendt host key. Workflowet
kan også startes manuelt på `main`. Migrationen kører aldrig fra en PR.

VPS-verifikationen henter to eksisterende målinger og gentager deres batch
to gange gennem den installerede `ingest.py`'s `on_message`. Begge faktiske
HTTP-skrivninger skal lykkes, WAL skal være anvendt, og antal/værdier skal
være uændrede. Den bruger servicens Python-interpreter; ingen testdata,
servicecredentials eller broker-genstart er nødvendige. Webdeployet berøres ikke.

## Tests

```bash
python -m pip install -r cloud/mqtt/requirements-dev.txt
python -m pytest cloud/mqtt/tests
```

Integrationstesten kræver en **tom, isoleret** QuestDB-instans og afviser en
instans, der allerede har `sensor_readings`:

```bash
QUESTDB_INTEGRATION_URL=http://127.0.0.1:19078 python -m pytest cloud/mqtt/tests/test_dedup_integration.py
```

Testen sender samme batch to gange gennem den rigtige ingest-kode, verificerer
separate enheder ved samme tid, last-write-wins og en gentagen migration.


## Vedvarende ingest-session (#81)

Ingest bruger MQTT v5 med det faste client-id `projekt-c-questdb-ingest`,
`clean_start=False` og `SessionExpiryInterval=86400` (ét døgn). Dermed
bevarer Mosquitto abonnementet og køer QoS 1-beskeder under stop, genstart
og deploy. Det gælder også den første forbindelse fra en ny Python-proces.
Ingest abonnerer fortsat med QoS 1 efter hver forbindelse; dette opdaterer
abonnementet uden at kassere ventende beskeder. To ingest-processer må ikke
køre samtidig med samme client-id, da de ellers overtager hinandens forbindelse.

Sessionen skal have været forbundet og have fået SUBACK mindst én gang,
før offline-levering er beskyttet. Efter ét døgn uden ingest udløber sessionen.
Brokerens `max_queued_messages` og eventuelle `max_queued_bytes` kan sætte
en tidligere grænse. Mosquittos standard er 1000 ventende QoS 1/2-beskeder
pr. klient; grænsen er antal MQTT-beskeder, ikke målinger. En batch kan derfor
gemme op til 20 målinger på samme køplads. Vi ændrer ikke broker-konfigurationen
eller genstarter brokeren for denne ændring. Broker-persistence beskytter ved
normal broker-genstart; strømsvigt kan stadig miste data siden seneste disksave.

Det tilføjer kun sessionegenskaber ved ingest-connect. Enheden sender samme
payload og batches som før. Dubletter ved genlevering håndteres af #78.
Enhedens PUBACK er fortsat brokerens kvittering, og ingest kvitterer automatisk
efter callback: fejlet HTTP-skrivning til QuestDB er en særskilt risiko (#88), som
sessionen alene ikke afhjælper.

Se [Pahos connect-API](https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html)
og [Mosquittos kø- og persistencegrænser](https://mosquitto.org/man/mosquitto-conf-5.html).

### Deploy og testcase 4

Workflowet [`Vedvarende ingest-session`](../../.github/workflows/ingest-session.yml)
tester stop/genstart af den rigtige ingest-proces mod isoleret Mosquitto 2.0.22
og QuestDB 10.0.1. To batches med 40 målinger og en gentagelse publiceres,
mens processen er stoppet. Testen kræver broker-PUBACK og nul databaserækker
før genstart; bagefter kræves alle 40 rækker med korrekte værdier.

VPS-administrator skal først installere den [afgrænsede root-helper](deploy/README.md),
da GitHub-kontoens eksisterende sudo-adgang kun dækker Nginx.
Efter merge deployes kun `ingest.py` til stien fra `projekt-c-ingest`'s
ExecStart. Filen erstattes atomisk med bevaret ejer og rettigheder, og servicens
QoS 1-abonnement skal blive etableret. Ved fejl gendannes den tidligere fil,
og servicen genstartes. Identisk indhold giver ingen genstart. Credentials,
service-unit, QuestDB og webdeploy berøres ikke; SSH bruger kun GitHub Secrets. Workflowet kører den installerede
root-ejede helper gennem sudo, aldrig uploadede helpermoduler som root.

Manuel kørsel på `main` med `verify_outage=true` gentager testcase 4 på VPS'en:
360 s stoppet ingest, ingen nye Thingy-rækker under stoppet, derefter mindst
én genleveret batch med målinger fra udfaldet og uden huller/dubletter.
Testen kræver en aktiv Thingy med standardprofilen (15 s, 20 målinger).
Et `finally`-forløb starter ingest igen, også ved testfejl. Workflowloggen
indeholder kødirektiver og de genleverede rækker til kontrol; ingen syntetiske
målinger indsættes. Testen udføres kun ved dette eksplicitte valg, ikke ved
normale deploys.
