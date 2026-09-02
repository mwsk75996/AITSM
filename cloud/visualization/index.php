<!doctype html>
<html lang="da">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta name="color-scheme" content="light dark">
    <meta name="description" content="Sensordata og målingshistorik fra AITSM Engineering">
    <title>Sensorer · AITSM Engineering</title>
    <link rel="stylesheet" href="styles.css">
    <script src="app.js" defer></script>
</head>
<body>
    <a class="skip-link" href="#indhold">Spring til indhold</a>
    <div class="loadbar" id="loadbar" aria-hidden="true"></div>

    <header class="app-header">
        <div class="app-header-inner">
            <a class="brand" href="./">
                <span class="brand-mark" aria-hidden="true">A</span>
                <span class="brand-text">
                    <span class="brand-name">AITSM</span>
                    <span class="brand-sub">Engineering</span>
                </span>
            </a>

            <p class="status" role="status" aria-live="polite">
                <span class="badge" id="status-badge" data-state="pending">
                    <span class="badge-dot" aria-hidden="true"></span>
                    <span id="status-label">Forbinder</span>
                </span>
            </p>
        </div>
    </header>

    <main class="container" id="indhold">
        <section class="hero">
            <p class="eyebrow" id="eyebrow-message">Live fra feltet!</p>
            <h1>Alle<br>målinger</h1>
            <p class="lead">Friske tal fra vores Nordic Thingy:91 X-enheder. Batch-data vises som individuelle målinger, så historikken bliver bevaret.</p>
        </section>

        <section class="kpi-grid" aria-label="Nøgletal">
            <article class="kpi" data-color="1">
                <h2 class="kpi-label">Målinger</h2>
                <p class="kpi-value" id="reading-count">—</p>
                <p class="kpi-hint" id="reading-count-hint">Henter historik …</p>
            </article>

            <article class="kpi" data-color="2">
                <h2 class="kpi-label">Gns. temperatur</h2>
                <p class="kpi-value" id="avg-temperature">—</p>
                <p class="kpi-hint">På tværs af målinger</p>
            </article>

            <article class="kpi" data-color="3">
                <h2 class="kpi-label">Laveste batteri</h2>
                <p class="kpi-value" id="min-battery">—</p>
                <p class="kpi-hint" id="min-battery-hint">Ingen data endnu</p>
            </article>

            <article class="kpi" data-color="4">
                <h2 class="kpi-label">Senest opdateret</h2>
                <p class="kpi-value" id="last-updated">—</p>
                <p class="kpi-hint">Hvert 30. sekund</p>
            </article>
        </section>

        <section aria-labelledby="readings-title">
            <div class="section-bar">
                <div>
                    <h2 id="readings-title">Målingshistorik</h2>
                    <p class="section-description">Alle registreringer, nyeste først. Batch-målinger vises enkeltvis.</p>
                </div>
            </div>

            <div class="table-card" id="table-card">
                <table class="table" role="table" aria-labelledby="readings-title" aria-busy="true" id="readings-table">
                    <thead role="rowgroup">
                        <tr role="row">
                            <th role="columnheader" scope="col">Enhed</th>
                            <th role="columnheader" scope="col">Tidspunkt</th>
                            <th role="columnheader" scope="col" class="col-numeric">Enhedstemperatur</th>
                            <th role="columnheader" scope="col" class="col-numeric">Batteri</th>
                        </tr>
                    </thead>
                    <tbody role="rowgroup" id="readings-body"></tbody>
                </table>
            </div>
        </section>
    </main>

    <footer class="container site-footer">
        <span>AITSM Engineering ApS</span>
        <span>QuestDB · <code>sensor_readings</code></span>
    </footer>
</body>
</html>
