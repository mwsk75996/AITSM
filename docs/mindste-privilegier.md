# Mindste privilegier

Kortlægning af alle dele af systemet efter princippet om mindste privilegier
(least privilege): hver del må kun have de rettigheder og ressourcer, den har
brug for (#107). Kortlægningen understøtter IEC 62443-vurderingen og CRA 1.h i
[redegørelsen](standarder-iec62443-cra.md).

Gennemgået og strammet 2026-10-29 for Simple og den fælles cloud.

## Kortlægning

| Del | Har brug for | Har nu |
| --- | --- | --- |
| Thingy:91 X (firmware) | Forbinde ud til 8883 og sende til egne emner | Kun én udgående TLS-forbindelse, ingen lyttende porte og ingen indgående kommandoer |
| MQTT-bruger `thingy91x` | Skrive til egne SparkplugB-emner | Skriver kun til `spBv1.0/aitsm/NBIRTH/thingy91x` og `spBv1.0/aitsm/NDATA/thingy91x` |
| MQTT-bruger `cloud_ingest` | Læse telemetri | Kun læseadgang |
| Mosquitto (container) | Lytte på 8883 og skrive sin session | Egen bruger, read-only filsystem, ingen capabilities, `no-new-privileges`, 64 MB og 32 processer |
| Ingest (container) | Læse fra brokeren og skrive til QuestDB | Egen bruger, read-only filsystem, ingen capabilities, kun internt netværk, 64 MB og 32 processer |
| QuestDB (container) | Gemme data, tage imod skrivning fra ingest og læsning fra websitet | Egen bruger, read-only filsystem med `/tmp` som tmpfs, ingen capabilities, kun udgivet på `127.0.0.1`, 768 MB og 128 processer |
| Website (`api.php`, `www-data`) | Læse seneste målinger | Læser via QuestDB's HTTP-API på `127.0.0.1` (se bevidst fravalgt) |
| VPS-tjenester | Kun det, projektet bruger | SSH, nginx med PHP-FPM og Docker. Udadtil er kun 22, 80, 443 og 8883 åbne |
| Deploy-bruger (GitHub Actions) | Køre to deploy-scripts | sudo kun til `aitsm-deploy-nginx` og `aitsm-deploy-ingest` |

Detaljerne for containerne står i [`cloud/docker/README.md`](../cloud/docker/README.md).

## Strammet

| Ændring | Før | Efter |
| --- | --- | --- |
| Ubrugt admin-webside | phpMyAdmin fra Webdocks standardopsætning var tilgængelig fra internettet | Fjernet fra webroden (404) |
| Ubrugte tjenester | MariaDB, Redis og Memcached kørte uden at blive brugt | Stoppet og slået fra |
| ACL for `thingy91x` | Havde også skriveadgang til det gamle emne `aitsm/+/telemetry`, hvor `+` tillod data på vegne af andre enheds-id'er | Kun egne SparkplugB-emner |
| Mosquitto og QuestDB | Alle standard-capabilities og skrivbart filsystem | Låst ned som ingest |
| Ressourcer | Kun QuestDB's JVM-heap var begrænset | Grænser for hukommelse og processer på alle containere |

Ændringerne er testet i drift: Thingy'en forbandt igen efter genstarten af
containerne, og QuestDB havde en række hvert 15. sekund uden huller og
dubletter.

## Bevidst fravalgt

Disse punkter er vurderet og fravalgt, fordi gevinsten ikke står mål med
kompleksiteten ved vores SL-T 1, og fordi kravspecifikationen udelukker
avanceret cloud-drift.

- **Adskillelse af læse- og skriveadgang til QuestDB.** QuestDB's HTTP-API har
  i dag ingen login, så alt, der kører på VPS'en, kan både læse og skrive via
  `127.0.0.1:9000`. Den kan ikke nås fra internettet, så en angriber skal
  allerede have adgang til serveren. En løsning er testet lokalt med samme
  QuestDB-version (10.0.1):
  - Websitet læser via PostgreSQL-protokollen med en read-only-bruger
    (`pg.readonly.user`). `SELECT` virker, mens `INSERT` og `DROP` afvises med
    "Write permission denied".
  - Ingest skriver via HTTP med login (`http.user`). Uden login giver HTTP 401.
  - HTTP's read-only-tilstand (`http.security.readonly`) kan ikke bruges, fordi
    den også blokerer ingests `/write`.

  Løsningen kræver ændringer i `ingest.py`, `api.php` og QuestDB-opsætningen og
  tre ekstra passwords. Den er videre arbejde.
- **Begrænsning af deploy-nøglen** (`restrict` i `authorized_keys`). sudo er
  allerede begrænset til de to deploy-scripts.
- **Administratorer.** `codex` beholder fuld sudo under udviklingen og slås fra
  før eksamen. Root-login med nøgle slås fra samtidig.
