# Visualization

Denne mappe indeholder den første version af visualization-websitet.

- `index.php` – websidens entrypoint
- `styles.css` – layout og styling
- `app.js` – henter og viser seneste måling pr. chip
- `api.php` – read-only endpoint mod QuestDB-tabellen `sensor_readings`

API’et bruger felterne `device_id`, `timestamp`, `temperature` og `battery`.
Det henter den seneste række for hver chip og opdateres automatisk hvert 30. sekund.

Lokalt kan siden testes med PHP’s indbyggede server fra projektroden:

```bash
php -S localhost:8080 -t cloud/visualization
```

Åbn derefter <http://localhost:8080>.

## GitHub Actions

Workflowet kører ved push til `main`, når `cloud/visualization/**` er ændret.
Det kræver disse GitHub Secrets:

- `VPS_HOST`
- `VPS_PORT` (valgfri, ellers bruges port 22)
- `VPS_USER`
- `VPS_PATH`
- `VPS_SSH_KEY`
- `VPS_KNOWN_HOSTS`

Deploy-brugeren skal kunne skrive til `/var/www/html/projekt-c/cloud/visualization/`
og have en begrænset, passwordless sudoers-regel til de to Nginx-kommandoer.
På VPS’en skal reglen tilpasses de faktiske binærstier, typisk:

```text
deploy-bruger ALL=(root) NOPASSWD: /usr/sbin/nginx -t, /usr/bin/systemctl reload nginx
```
