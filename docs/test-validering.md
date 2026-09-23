# Test og validering (issue #12)

Denne rapport beskriver testen af hele kæden fra sensor over databehandling, batching, MQTT og TLS til broker og QuestDB, inklusive reconnect-scenarier.

## Testopstilling

| | |
|---|---|
| Dato | 2026-09-23, 10:09–11:03 UTC |
| Enhed | Nordic Thingy:91 X (`THINGY91X_0ABA4D24A1A`) over USB-C |
| Firmware | Applikationskode fra `main` (`084c779`), bygget med [`app/overlay-at-shell.conf`](../app/overlay-at-shell.conf) |
| Profil | Batch, måling hvert 15. s, maks. 20 målinger / 300 s pr. batch, QoS 1 |
| Modemfirmware | `mfw_nrf91x1_2.0.2` |
| Netværk | NB-IoT, roaming på PLMN 238-01, bånd 20 |
| Signal | RSRP ≈ −67 dBm, RSRQ ≈ −1,5 dB (`AT%XMONITOR`) |
| Cloud | Mosquitto (`persistence true`), `projekt-c-ingest` (samme `ingest.py` som i repo), QuestDB 10.0.1 |

### Metode

- Test-overlayet giver en shell på USB-konsollen med `at <kommando>` og `kernel reboot cold`. Det bruges kun til test og er ikke en del af standardbuildet.
- Enhedens log blev optaget med UTC-tidsstempel fra værten pr. linje.
- For hvert tidsvindue blev hver `Måling indsamlet`-linje i loggen sammenlignet med rækkerne i `sensor_readings` for `device_id = 'thingy91x'`. Sammenligningen omfatter antal, rækkefølge, temperatur, batteri og tidsforskel, samt huller over 20 s og dubletter.
- Indgreb på VPS'en (genstart af Mosquitto, stop af ingest) blev udført med `systemctl` over SSH.

Build og flash af testfirmwaren:

```bash
west build -b thingy91x/nrf9151/ns -d build/thingy91x_at_shell app \
  -- -DEXTRA_CONF_FILE=overlay-at-shell.conf
nrfutil device program --firmware build/thingy91x_at_shell/dfu_application.zip \
  --serial-number <id> --traits mcuBoot --family nrf91 \
  --options target=nRF91,mcu_end_state=NRFDL_MCU_STATE_APPLICATION
```

## Resultater

| # | Scenarie | Resultat | Fund |
|---|---|---|---|
| 1 | Happy path E2E (30 min) | ✅ Bestået | – |
| 2 | LTE-udfald (`AT+CFUN=4` i 2 min) | ✅ Bestået med forbehold | #77, #82 |
| 3 | Broker-genstart | ❌ Fejlet | #76 |
| 4 | Ingest stoppet i 6 min | ❌ Fejlet, data tabt | #81 |
| 5 | Forkert MQTT-password | ⚠️ Delvist | #82 |
| 6 | Samme payload leveret to gange | ❌ Dubletter | #78 |
| 7 | Payload-grænse | ✅ Unit tests, fandt Kconfig-fejl | #79 |

### 1. Happy path E2E

10:09:30–10:39:35 UTC: 120 målinger på enheden, 120 rækker i QuestDB og 6 batches bekræftet med PUBACK. Ingen afvigelser i værdier eller tid, ingen huller, ingen dubletter og ingen `<wrn>`/`<err>` i loggen.

### 2. LTE-udfald

`AT+CFUN=4` 10:40:05, `AT+CFUN=1` 10:42:05.

- MQTT-socket lukkede med det samme (`-113`), og `LTE not registered` blev logget.
- Efter `CFUN=1`: LTE registreret efter 2 s og MQTT forbundet efter 8 s ("med eksisterende session").
- De 2 målinger fra før udfaldet blev bevaret og sendt med næste batch (13 af 13 rækker i QuestDB).
- Hul i dataene 10:39:53–10:42:13, fordi der ikke måles, mens MQTT er nede (#77).
- Rækkefølgen af events får LED'en til at vise "LTE forbundet" under hele udfaldet (#82).

### 3. Broker-genstart

`systemctl restart mosquitto` 10:45:28. Enheden fik `-128` (forbindelse lukket) og forblev offline uden et eneste reconnect-forsøg i over 5 minutter, selvom LTE var oppe (#76). Genoprettet manuelt med `AT+CFUN=4/1`, hvorefter MQTT var oppe 8 s efter `CFUN=1`. Målingerne fra før genstarten, som lå i bufferen, kom frem i QuestDB.

Ingest-servicen genstartes automatisk sammen med Mosquitto (`Requires=mosquitto.service`).

### 4. Ingest-nedbrud

`projekt-c-ingest` stoppet 10:51:57–10:57:59. Enheden sendte en batch med 20 målinger 10:56:15. Mosquitto bekræftede den, og enheden fjernede målingerne fra bufferen. **Ingen af de 20 målinger nåede QuestDB**, heller ikke efter ingest startede igen. Ingest bruger ikke en vedvarende MQTT-session, så brokeren gemmer ikke beskeder til den (#81).

### 5. Forkert credential

Firmware bygget med forkert `AITSM_MQTT_PASSWORD`:

- Tydelig log: `MQTT-forbindelse afvist, return code: 5` (not authorized).
- Ingen crash og intet reboot-loop (observeret i 2,5 min).
- Intet nyt connect-forsøg (se #76).
- Fejl-LED'en overskrives med det samme af "LTE forbundet" ved det efterfølgende disconnect-event (#82).

### 6. Tabt PUBACK / samme payload to gange

Et tabt PUBACK er svært at fremprovokere, så gentagen levering blev testet direkte mod QuestDB. Det skete med to midlertidige tabeller med samme skema som `sensor_readings` (WAL), som bagefter blev slettet. Samme batch med 2 målinger blev skrevet to gange via `/write`:

| Tabel | Rækker |
|---|---|
| Som produktion (WAL, uden DEDUP) | 4 |
| Med `ALTER TABLE ... DEDUP ENABLE UPSERT KEYS(timestamp, device_id)` | 2 |

Produktionstabellen havde ingen dubletter (3160 rækker, 3160 unikke). Løsningen er beskrevet i #78.

### 7. Payload-grænse

Dækket af unit tests i `tests/data_transmission` (se [`tests/README.md`](../tests/README.md)). Worst-case-testen viste, at Kconfig tillader kombinationer, hvor en fuld batch ikke kan sendes, f.eks. 64 målinger med 2048 bytes (#79).

## Automatiserede tests

- `west twister -p native_sim/native/64 -T tests/data_transmission -T tests/led_status`: 14/14 bestået.
- `cloud/mqtt`: `pytest`: 51/51 bestået.

## Fund

| Issue | Fund |
|---|---|
| #76 | MQTT forbinder ikke igen, når LTE stadig er oppe |
| #77 | Ingen målinger under MQTT-udfald |
| #78 | Dubletrækker i QuestDB ved gentagen levering |
| #79 | En fuld batch kan overstige payloadbufferen |
| #81 | Målinger tabes, mens ingest-servicen er nede |
| #82 | LED viser LTE forbundet efter MQTT-fejl og under LTE-udfald |

Testcase 2–6 bør køres igen, når fundene er rettet.
