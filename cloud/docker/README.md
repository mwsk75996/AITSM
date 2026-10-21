# Mosquitto, QuestDB og ingest i Docker

MQTT-brokeren, QuestDB og ingest kører som containere på VPS'en, styret af
`compose.yml`. Nginx og PHP-FPM kører fortsat direkte på værten, så webdeploy
og `api.php` er uændrede.

```
Thingy:91 X ──MQTT/TLS :8883──▶ mosquitto ──backend :1883──▶ ingest
                                                               │ backend HTTP /write
Browser ──HTTPS :443──▶ Nginx ──▶ api.php ──127.0.0.1:9000──▶ questdb
```

| Service | Image | Porte på værten | Data |
| --- | --- | --- | --- |
| `mosquitto` | `eclipse-mosquitto:2.0.22` | `8883` (alle) | `/var/lib/mosquitto` |
| `questdb` | `questdb/questdb:10.0.1` | `127.0.0.1:9000` | `/var/lib/questdb` |
| `ingest` | `aitsm-ingest:local` (`ingest/Dockerfile`) | ingen | – |

Mosquitto og QuestDB er låst til samme digest som CI-workflowene, så der testes
mod den version, der kører i drift. Ingest-imaget er Python 3.12 med
`paho-mqtt==2.1.0`; selve `ingest.py` bind-mountes fra værten.

## Valg

- **To netværk.** `edge` er det almindelige bridge-netværk med de udgivne
  porte. `backend` er `internal`: ingen udgivne porte og ingen vej til
  internettet. Ingest er kun på `backend` og når brokeren og databasen via
  servicenavnene `mosquitto` og `questdb`. Port 1883 udgives ikke længere på
  værten.
- **Ingest-koden mountes, den bygges ikke ind.** Deploy-helperen
  `aitsm-deploy-ingest` udskifter `ingest.py` atomisk i
  `/var/www/html/projekt-c/cloud/mqtt/` og genstarter containeren med
  `docker compose restart ingest`. Mappen mountes i stedet for filen, fordi en
  bind-mount af en enkelt fil holder fast i den gamle inode.
- **Ingest kører låst ned.** Som værtens `projektc`-bruger (995:986),
  read-only rodfilsystem, `cap_drop: [ALL]` og `no-new-privileges`.
  `init: true` giver tini som PID 1, så SIGTERM når Python og stop tager
  ~1 s i stedet for 10 s.
- **Credentials bliver på værten.** `env_file: /etc/projekt-c/mqtt.env`
  (root 600) læses af Docker ved opstart; filen kopieres ikke ind i imaget.
- **Samme datamapper som før.** Containerne bind-mounter de eksisterende
  mapper og kører som værtens `mosquitto`- (113:116) og `questdb`-bruger
  (997:987). Der kopieres ingen data, og filrettighederne passer uden `chown`.
- **`/etc/mosquitto` mountes read-only.** `passwd`, `acl` og certifikater
  vedligeholdes på værten som før; `mosquitto.conf` i dette repo erstatter
  `conf.d/aitsm.conf`.
- **Lokale porte bindes til `127.0.0.1`.** Docker skriver egne iptables-regler,
  så en port udgivet som `9000:9000` ville være åben fra internettet uanset
  UFW. Kun `8883` udgives på alle interfaces. QuestDB's øvrige porte
  (8812, 9009, 9003) udgives ikke.
- **QuestDB's `server.conf` binder til `127.0.0.1`**, som i en container kun
  er containeren selv. `QDB_HTTP_NET_BIND_TO` overskriver det for HTTP.

## Opsætning på VPS'en

```bash
sudo install -d -m 755 /opt/aitsm/docker/mosquitto /opt/aitsm/docker/ingest
sudo install -m 644 cloud/docker/compose.yml /opt/aitsm/docker/
sudo install -m 644 cloud/docker/mosquitto/mosquitto.conf /opt/aitsm/docker/mosquitto/
sudo install -m 644 cloud/docker/ingest/Dockerfile /opt/aitsm/docker/ingest/
cd /opt/aitsm/docker && sudo docker compose build && sudo docker compose up -d
```

Skiftet fra systemd (2026-10-20):

1. Mosquitto og QuestDB: `systemctl stop projekt-c-ingest mosquitto questdb`,
   backup af `/var/lib/questdb` og `/var/lib/mosquitto`, `systemctl disable
   questdb mosquitto` og `systemctl mask mosquitto`, så pakkeopdateringer ikke
   starter den gamle broker. `docker compose up -d`.
2. Ingest: `systemctl disable --now projekt-c-ingest`, unit-filen flyttet til
   `/root/projekt-c-ingest.service.docker-cutover-20261020`, `daemon-reload`,
   `docker compose up -d`. Ingest genoptog den vedvarende MQTT-session
   (`projekt-c-questdb-ingest`), så beskeder fra skiftet blev leveret bagefter.
3. Helpermodulerne i `/usr/local/lib/aitsm-ingest` geninstalleret, så deploy
   og udfaldstest bruger `docker compose` i stedet for `systemctl`.

Certbot-hooken `/etc/letsencrypt/renewal-hooks/deploy/mosquitto-cert-sync`
kopierer fortsat certifikatet til `/etc/mosquitto/certs` og genindlæser
derefter brokeren med:

```bash
docker compose -f /opt/aitsm/docker/compose.yml kill -s HUP mosquitto
```

## Drift

```bash
cd /opt/aitsm/docker
sudo docker compose ps
sudo docker compose logs -f ingest
sudo docker compose restart questdb
```

Containerne har `restart: unless-stopped` og starter igen efter genstart af
VPS'en. Ændringer i `ingest/Dockerfile` kræver
`docker compose build ingest && docker compose up -d`; ændringer i `ingest.py`
deployes af workflowet uden nyt image.

### Genstart af brokeren (#101)

Med systemd stoppede ingest sammen med Mosquitto og startede ikke igen
(`Requires=` og `Restart=on-failure`). I Docker er ingest ikke afhængig af
brokerens livscyklus: `depends_on` styrer kun rækkefølgen ved opstart, så
containeren bliver kørende, og paho-klienten forbinder selv igen. Den
vedvarende session (`projekt-c-questdb-ingest`) gør, at beskeder sendt imens
leveres bagefter.

Verificeret 2026-10-21 med Thingy'en kørende:

- `docker compose restart mosquitto`: ingest blev kørende og forbandt igen
  efter 2 s.
- `docker compose stop mosquitto`, 60 s pause og `docker compose start
  mosquitto`: ingest blev kørende under udfaldet og forbandt igen 2 s efter
  start.
- Thingy'en forbandt igen og sendte NBIRTH og én NDATA med 21 målinger.
  QuestDB havde derefter 87 rækker fra 06:40:07 til 07:01:37 UTC, dvs. én
  hvert 15. sekund uden huller og uden dubletter.

## Rollback

```bash
cd /opt/aitsm/docker && sudo docker compose down
sudo systemctl unmask mosquitto && sudo systemctl enable --now mosquitto questdb
sudo cp /root/projekt-c-ingest.service.bak-20261020 /etc/systemd/system/projekt-c-ingest.service
sudo cp /root/mosquitto-cert-sync.bak-20261020 /etc/letsencrypt/renewal-hooks/deploy/mosquitto-cert-sync
sudo systemctl daemon-reload && sudo systemctl enable --now projekt-c-ingest
```

Helpermodulerne fra før skiftet skal da også geninstalleres fra en tidligere
commit med `cloud/mqtt/deploy/install-helper.sh`.
