<!doctype html>
<html lang="da">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta name="description" content="AITSM Engineering visualization">
    <title>AITSM Visualization</title>
    <link rel="stylesheet" href="styles.css">
    <script src="app.js" defer></script>
</head>
<body>
    <main class="page-shell">
        <header class="topbar">
            <a class="brand" href="./" aria-label="AITSM Visualization forside">
                <span class="brand-mark">A</span>
                <span>AITSM Engineering</span>
            </a>
            <span class="status-pill" id="api-status" data-state="loading">
                <span class="status-dot" aria-hidden="true"></span>
                Forbinder...
            </span>
        </header>

        <section class="hero" aria-labelledby="page-title">
            <p class="eyebrow">Projekt C · Nordic Thingy:91 X</p>
            <h1 id="page-title">Data, der giver<br><span>overblik.</span></h1>
            <p class="intro">En enkel visualisering af IoT-platformens status og dataflow.</p>
        </section>

        <section class="overview-grid" aria-label="Systemoverblik">
            <article class="overview-card overview-card--accent">
                <p class="card-label">Forbindelse</p>
                <h2 id="api-state">Kontrollerer...</h2>
                <p class="card-detail" id="api-message">Venter på svar fra visualization API.</p>
            </article>

            <article class="overview-card">
                <p class="card-label">Dataflow</p>
                <h2>MQTT</h2>
                <p class="card-detail">Thingy:91 X → cloud-platform</p>
            </article>

            <article class="overview-card">
                <p class="card-label">Seneste opdatering</p>
                <h2 id="last-updated">—</h2>
                <p class="card-detail">Tidspunkt fra API’et</p>
            </article>
        </section>

        <section class="empty-state" aria-labelledby="empty-state-title">
            <div class="empty-icon" aria-hidden="true">⌁</div>
            <div>
                <p class="eyebrow">Visualisering</p>
                <h2 id="empty-state-title">Klar til de første målinger</h2>
                <p>Dashboardet er online. Når dataforbindelsen er koblet på, vises målingerne her.</p>
            </div>
        </section>

        <footer class="footer">
            <span>AITSM Engineering ApS</span>
            <span>Visualization · <span id="year"></span></span>
        </footer>
    </main>
</body>
</html>
