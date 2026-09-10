# Visualization

Denne mappe indeholder visualization-websitet: en **React + Vite + TypeScript**-app
med **shadcn/ui**-komponenter (Tailwind CSS). PHP-backenden ligger fortsat i
`public/api.php` og bygges med ud til `dist/`.

## Struktur

- `index.html` – Vite-entrypoint
- `src/` – React-kildekode
  - `App.tsx` – layout og sammensætning af siden
  - `components/ui/` – shadcn-komponenter (button, card, badge, table, select, skeleton, input)
  - `components/` – app-specifikke komponenter (header, filtre, KPI-kort, tabel, batteri, paginering)
  - `hooks/use-readings.ts` – datahentning, filtre, keyset-paginering og auto-refresh
  - `lib/` – API-klient, formatering og typer
- `public/api.php` – read-only endpoint mod QuestDB-tabellen `sensor_readings`
- `vite.config.ts` – `base: './'` samt dev-proxy for `/api.php`

API’et bruger felterne `device_id`, `timestamp`, `temperature` og `battery` og
returnerer desuden `total`, `has_more`, `next_cursor` og `summary`. Siden henter
historikken med keyset-paginering og opdaterer automatisk hvert 30. sekund.

### Filtre

Ud over `page_size` og `cursor` understøtter `api.php` disse valgfrie filtre.
Uden filtre opfører endpointet sig som før (fuld historik, nyeste først).

| Parameter | Betydning |
| --- | --- |
| `date` | Dag i UTC, `YYYY-MM-DD` (kan ikke kombineres med `from`/`to`) |
| `from` / `to` | Tidsinterval, ISO-8601 (`from` inklusiv, `to` eksklusiv) |
| `device_id` | Begræns til én enhed |
| `temperature_min` / `temperature_max` | Nedre/øvre grænse for temperatur |
| `battery_min` / `battery_max` | Nedre/øvre grænse for batteri |

`total` og `summary` afspejler de valgte filtre. Ugyldige værdier afvises med
HTTP 400 og en forklarende fejlbesked.

## Lokal udvikling

Installer afhængigheder:

```bash
cd cloud/visualization
npm install
```

Kør PHP-endpointet og Vite-dev-serveren i hver sin terminal. Vite proxyer
`/api.php` videre til PHP, så frontend altid kalder samme sti:

```bash
# Terminal 1 – PHP-backend
php -S 127.0.0.1:8080 -t cloud/visualization/public

# Terminal 2 – Vite-dev-server
cd cloud/visualization && npm run dev
```

Åbn <http://localhost:5173>.

Vil du teste det færdige build med PHP, kan `dist/` serveres direkte (her ligger
både `index.html` og `api.php`):

```bash
cd cloud/visualization && npm run build
php -S localhost:8080 -t cloud/visualization/dist
```

Åbn derefter <http://localhost:8080>.

## Scripts

```bash
npm run dev      # Vite-dev-server
npm run build    # type-check + produktionsbuild til dist/
npm run lint     # oxlint
npm run preview  # preview af build (PHP kører ikke her)
```

## GitHub Actions

Workflowet kører ved push til `main`, når `cloud/visualization/**` er ændret.
Det installerer Node, kører `npm ci`, `npm run lint` og `npm run build`,
synkroniserer `cloud/visualization/dist/` til VPS’en og opdaterer derefter
Nginx-konfigurationen via det root-ejede wrapper-script.

Det kræver disse GitHub Secrets:

- `VPS_HOST`
- `VPS_PORT` (valgfri, ellers bruges port 22)
- `VPS_USER`
- `VPS_PATH`
- `VPS_SSH_KEY`
- `VPS_KNOWN_HOSTS`

### Nginx som kode

Nginx-snippeten for visualizationen versionsstyres i `cloud/visualization/deploy/`
og installeres ved hvert deploy til `/etc/nginx/snippets/aitsm-visualization.conf`.
Deploy-brugeren skal kunne skrive til `/var/www/html/projekt-c/cloud/visualization/`
og have en passwordless sudoers-regel, der kun tillader det faste wrapper-script:

```text
aitsm-deploy ALL=(root) NOPASSWD: /usr/local/bin/aitsm-deploy-nginx
```

Se `cloud/visualization/deploy/README.md` for engangsopsætningen af serveren
(wrapper-script, sudoers-regel og `include`-linjen i vhosten).

