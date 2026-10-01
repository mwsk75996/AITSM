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
lagring under ingest-udfald behandles i #81.

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
