# Standarder: IEC 62443-4-2 og CRA

Denne redegørelse dækker issue #11. Formålet er ikke fuld compliance, men at
udvælge relevante punkter fra IEC 62443-4-2 og artikler fra CRA Annex I og
argumentere for, hvordan løsningen adresserer dem. Referencerne til
IEC 62443-4-2 er fra [`docs/references/standards/iec-62443-4-2.pdf`](references/standards/iec-62443-4-2.pdf).

## IEC 62443-4-2

### CR 3.1 – Communication integrity (+ enhancement (1) Communication authentication)

**Krav:** Komponenten skal beskytte integriteten af transmitteret information
og kunne verificere afsenderens autenticitet.

**Hvordan løsningen adresserer det:** Al telemetri sendes fra Thingy:91 X til
cloud-broker'en over MQTT med TLS på port 8883
(`app/src/mqtt_client.c`, `AITSM_MQTT_HOSTNAME`/port 8883 i
`aitsm_mqtt_init()`). CA-certifikatet provisioneres til modemets
sikkerhedslager før forbindelsen oprettes
(`app/src/mqtt_credentials_provision.c`, `modem_key_mgmt_write(...)`), så
enheden kan verificere broker'ens certifikat under TLS-håndtrykket.
Forbindelsen autentificeres derudover med et device-specifikt klient-id samt
brugernavn/password (`conn_params` i `mqtt_client.c`), hvilket dækker
enhancement (1).

**Afgrænsning:** Der er ikke implementeret klientcertifikater (mTLS) — kun
server-autentificering via TLS plus MQTT-brugernavn/password. Det er
tilstrækkeligt til det aktuelle trusselsbillede, men nævnes som en mulig
fremtidig forbedring.

### CR 4.1 – Information confidentiality

**Krav:** Beskyt fortroligheden af information på kommunikationskanaler og i
lagret data.

**Hvordan løsningen adresserer det:** Samme TLS-forbindelse som ovenfor
sikrer fortrolighed for data i transit mellem enhed og broker. Der lagres
ingen følsomme data lokalt på enheden ud over MQTT-credentials, som ligger i
en ikke-committed `mqtt_credentials.h` (se `#if __has_include(...)` i
`mqtt_client.c`) og modemets sikkerhedslager.

### CR 3.7 – Error handling

**Krav:** Fejltilstande skal identificeres og håndteres uden at afsløre
information, som en modstander kan udnytte til at angribe systemet.

**Hvordan løsningen adresserer det:** Fejl i MQTT-laget logges udelukkende
som numeriske return codes/fejlkoder — ikke som beskrivende tekststrenge, der
kunne afsløre systemdetaljer:

```c
LOG_ERR("MQTT-forbindelse afvist, return code: %d", return_code);
LOG_ERR("MQTT-helper fejl: %d", error);
LOG_ERR("MQTT-målepayload blev ikke bekræftet, message id: %u, resultat: %d", ...);
```

(`app/src/mqtt_client.c`). Fejl propageres videre som events til
`app_controller.c`, som også kun logger koden, ikke en fortolket årsag.
Dermed er der ikke nogen oplagt kilde til information, en angriber kunne
bruge til at målrette et angreb (fx skelne "forkert login" fra "forkert
certifikat").

**Afgrænsning:** Der er endnu ikke implementeret automatisk
reconnect/backoff ved `AITSM_APP_EVENT_MQTT_ERROR` i `app_controller.c` —
fejlen logges, men der forsøges ikke aktivt genoprettelse. Det er et
funktionelt hul snarere end et sikkerhedshul, men det er relevant for CR 3.7's
naboer (fx CR 7.1 – Resource availability), hvis de skal dækkes senere.

### CR 2.8 – Auditable events

**Krav:** Komponenten skal generere audit-records for bl.a. access control,
request errors, control system events og konfigurationsændringer, med
timestamp, kilde, type og resultat.

**Hvordan løsningen adresserer det:** Applikationen logger de centrale
hændelser i sin livscyklus via Zephyrs logging-subsystem
(`LOG_MODULE_REGISTER` i både `mqtt_client.c` og `app_controller.c`):
forbindelse etableret/afvist/lukket, publish bekræftet/fejlet, og
initialiseringsfejl. Det dækker i praksis kategorierne "request errors" og
"control system events" fra kravet.

**Afgrænsning:** Dette er ikke et fuldt audit-log-system. Loggen er kun
tilgængelig lokalt via seriel/RTT-output og persisteres ikke, hvilket betyder
at kravene til lagringskapacitet (**CR 2.9**), respons på audit-svigt
(**CR 2.10**) og ekstern tilgængelighed af audit-logs (**CR 6.1**) bevidst
*ikke* er omfattet af denne redegørelse — de kræver infrastruktur, projektet
ikke har bygget endnu.

## CRA Annex I

### 1.c – Beskyttelse mod uautoriseret adgang

Kræver passende kontrolmekanismer, herunder autentificering og identity
management. Adresseres af den samme MQTT-autentificering som CR 3.1: unikt
klient-id (`AITSM_MQTT_CLIENT_ID`) samt brugernavn/password valideret af
broker'en, kombineret med TLS-forbindelsen der forhindrer aflytning af
credentials under transport.

### 1.d – Beskyttelse af fortrolighed

Kræver kryptering af data i transit/at rest efter state-of-the-art metoder.
Adresseres af TLS på MQTT-forbindelsen (samme grundlag som CR 4.1). Der
opbevares ikke andre følsomme data lokalt på enheden, som kræver kryptering
at rest.

### 1.h – Minimér negativ indflydelse på andre enheders/netværks tilgængelighed

Kræver at produktet ikke belaster andre enheder eller netværk unødigt (fx
ved at bidrage til DoS). Delvist adresseret: publisering sker med QoS 1
(`MQTT_QOS_1_AT_LEAST_ONCE` i `aitsm_mqtt_publish_payload()`), så enheden
ikke spammer gentagne fulde forbindelsesforsøg for hver besked. Der er dog
**ingen backoff-strategi implementeret endnu** ved gentagne forbindelsesfejl
— hvis broker'en er nede, vil enheden potentielt forsøge at genoprette
forbindelse uden stigende ventetid. Dette er et konkret forbedringspunkt,
ikke noget der kan erklæres opfyldt i dag.

### 1.j – Reducér konsekvenser af sikkerhedshændelser

Kræver mekanismer, der begrænser skadesomfanget af en hændelse (exploitation
mitigation). Adresseres delvist af fejlhåndteringen under CR 3.7 (ingen
lækage af udnyttelig information ved fejl) samt af, at enheden kører på
Zephyr RTOS med memory protection via `CONFIG_TRUSTED_EXECUTION_NONSECURE`
(se README's afsnit om non-secure board-target). Automatisk
recovery/fail-safe-adfærd ved vedvarende MQTT-fejl er endnu ikke bygget (jf.
afgrænsningen under CR 3.7).

### 2.1 – Identifikation og dokumentation af sårbarheder (SBOM)

Kræver dokumentation af komponenter og sårbarheder, typisk via en Software
Bill of Materials. **Dette punkt er ikke opfyldt endnu.** Der findes i dag
ingen SBOM for projektets afhængigheder (nRF Connect SDK-moduler, Zephyr,
`mqtt_helper` m.fl.). Det er den klareste mangel af de valgte artikler og et
oplagt fremtidigt arbejdspunkt — fx via `west`/Zephyrs indbyggede SBOM-
generering.

## Opsummering

| Punkt | Status |
|---|---|
| CR 3.1 – Communication integrity | Opfyldt (TLS + klient-id/login) |
| CR 4.1 – Information confidentiality | Opfyldt (samme TLS-kanal) |
| CR 3.7 – Error handling | Opfyldt (fejlkoder, ingen detaljelækage) |
| CR 2.8 – Auditable events | Delvist (lokal logging, ikke persisteret) |
| CRA 1.c | Opfyldt |
| CRA 1.d | Opfyldt |
| CRA 1.h | Delvist (mangler backoff-strategi) |
| CRA 1.j | Delvist (mangler fail-safe/recovery) |
| CRA 2.1 (SBOM) | Ikke opfyldt — fremtidigt arbejde |
