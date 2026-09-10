# Deploy af Project C visualization

Nginx-konfigurationen for visualizationen er versionsstyret i dette repo og
installeres af deploy-workflowet via et root-ejet wrapper-script. Det betyder, at
et deploy ikke længere afhænger af manuel serverkonfiguration.

## Filer

- `nginx-visualization.conf` – den snippet, der installeres til
  `/etc/nginx/snippets/aitsm-visualization.conf` og inkluderes fra vhosten.
- `aitsm-deploy-nginx` – root-ejet wrapper, der installerer snippeten, kører
  `nginx -t` og reloader Nginx. Workflowet kalder den med `sudo -n`.
- `aitsm-deploy.sudoers` – sudoers-regel, der kun tillader det faste
  wrapper-script for `aitsm-deploy`.

## Engangsopsætning på serveren

Kør som en bruger med sudo (fx `admin`):

```bash
# 1) Sørg for at snippet-mappen findes
sudo install -d -m 0755 /etc/nginx/snippets

# 2) Installér wrapper-scriptet (root-ejet, ikke skrivbar for deploy-brugeren)
sudo install -m 0755 -o root -g root \
  cloud/visualization/deploy/aitsm-deploy-nginx /usr/local/bin/aitsm-deploy-nginx

# 3) Installér og valider sudoers-reglen
sudo visudo -c -f cloud/visualization/deploy/aitsm-deploy.sudoers
sudo install -m 0440 -o root -g root \
  cloud/visualization/deploy/aitsm-deploy.sudoers /etc/sudoers.d/aitsm-deploy
```

Tilføj derefter én gang denne linje i serverens vhost (i `server { ... }`):

```nginx
include /etc/nginx/snippets/aitsm-visualization.conf;
```

og indlæs den:

```bash
sudo nginx -t && sudo systemctl reload nginx
```

Herefter klarer workflowet resten ved hvert deploy.
