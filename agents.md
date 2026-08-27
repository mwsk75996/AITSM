# Projektinstruktioner

## Git-kommunikation

- Arbejdet planlægges og følges via Issues.
- Ændringer udvikles i feature branches, som knyttes til den relevante Issue.
- Når den endelige PR for en feature oprettes, skal den lukke det tilhørende issue.
- Vi bruger ikke længere individuelle personlige branches — al kodning sker i feature branches.
- Ændringer skal ledsages af relevante unit tests, og unit tests skal køres, før arbejdet afsluttes.
- Koden skal have en god, modulær filstruktur. Funktionalitet skal opdeles i relevante `.c`- og `.h`-filer, f.eks. `feature.c` og `feature.h`, i stedet for at samle alt i `main.c`.
- Hvis brugeren siger “push til main”, skal det dobbelttjekkes, at det virkelig er den ønskede branch, før der pushes.
- Alle commitbeskeder skal skrives på dansk.
- Alle pull requests (PRs), herunder titel, beskrivelse og kommentarer, skal skrives på dansk.
- Issues, review-kommentarer og anden Git-relateret kommunikation skal skrives på dansk.
- Kode, filnavne og tekniske identifikatorer må fortsat bruge engelsk, når det er den naturlige konvention.

## Cloud og visualization

- Live visualization: [aitsm.vps.webdock.cloud](https://aitsm.vps.webdock.cloud/)
- Kildekode til websitet ligger i `cloud/visualization/`.
- Push til `main` med ændringer i `cloud/visualization/**` udløser GitHub Actions-workflowet `Deploy visualization`.
- Workflowet deployer kun til `/var/www/html/projekt-c/cloud/visualization/` via rsync over SSH.
- Website-entrypointet er `index.php`, og de tilhørende assets er `styles.css`, `app.js` og `api.php`.
- `api.php` læser read-only fra QuestDB-tabellen `sensor_readings` og viser seneste række pr. `device_id` med felterne `timestamp`, `temperature` og `battery`.
- Deploy må ikke ændre `cloud/mqtt/`, QuestDB-data, Mosquitto-konfiguration, `.env`-filer eller `/var/www/html/index.php`.
- SSH credentials og host key må kun håndteres via GitHub Secrets. Private keys og secret-værdier må aldrig committes eller dokumenteres i repository’et.
- Ved cloud-ændringer skal PHP-filer lintes, live-assets kontrolleres, og workflowets live-test skal bestå for `/`, `/styles.css`, `/app.js` og `/api.php`.
