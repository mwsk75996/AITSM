# Afgrænset ingest-deploy på VPS

Den eksisterende GitHub deploy-konto har kun sudo-adgang til webserverens
Nginx-wrapper. Den kan hverken skrive ingest-mappen eller genstarte ingest.
Administrator skal én gang installere denne separate helper fra et gennemgået
checkout på VPS'en:

```bash
bash cloud/mqtt/deploy/install-helper.sh DEPLOY_BRUGERNAVN
```

Kommandoen kræver root på VPS og bruger det eksisterende deploy-brugernavn
fra `VPS_USER`. Den ændrer ikke SSH-secrets, servicecredentials, den eksisterende
Nginx-wrapper eller broker-konfigurationen. Sudoers valideres med `visudo`.

`/usr/local/bin/aitsm-deploy-ingest` og de tre Python-helpermoduler installeres
root-ejet uden skriverettigheder til deploy-kontoen. Wrapperen accepterer kun
en `/tmp/aitsm-ingest.XXXXXX`-mappe med et regulært `ingest.py` og eventuelt
`--verify-outage`. Den kører den installerede helper, aldrig et uploadet
Python-helpermodul som root. Helperen kan kun ændre scriptet fra den faste
`projekt-c-ingest`-service og genstarte netop den service. Uploadet kode bliver
kørt som servicens konfigurerede bruger; adgang til denne wrapper er derfor
adgang til at deploye ingest-applikationen.

Workflowet bruger wrapperen med `sudo -n`. Indtil administrator har installeret
den, fejler deploy før serviceændringer. `inspect_only=true` kan køres på en
feature branch og læser kun fil-/sudo-rettigheder. Normalt deploy og den
valgfri seks minutters udfaldstest kan kun køres fra `main`.

Opdatering af root-helperne kræver administratorinstallation igen; workflowet
opdaterer kun applikationsfilen. Ingen rå passwords skal sendes i chatten.
