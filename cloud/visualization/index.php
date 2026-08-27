<!doctype html>
<html lang="da">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta name="description" content="Seneste sensordata fra AITSM Engineering">
    <title>Sensorer · AITSM Engineering</title>
    <link rel="stylesheet" href="styles.css">
    <script src="app.js" defer></script>
</head>
<body>
    <div class="page-shell">
        <header class="site-header">
            <a class="brand" href="./">AITSM Engineering</a>
            <div class="header-meta">
                <span class="status-dot" id="status-dot" aria-hidden="true"></span>
                <span id="status-label">Forbinder</span>
            </div>
        </header>

        <main>
            <section class="page-heading" aria-labelledby="page-title">
                <p class="eyebrow">IoT-monitorering</p>
                <h1 id="page-title">Seneste målinger</h1>
                <p>Aktuelle data fra tilsluttede Nordic Thingy:91 X-chips.</p>
            </section>

            <section class="panel" aria-labelledby="readings-title">
                <div class="panel-heading">
                    <div>
                        <h2 id="readings-title">Chips</h2>
                        <p id="reading-count">Henter data...</p>
                    </div>
                    <p class="last-updated" id="last-updated">—</p>
                </div>

                <div class="table-wrapper">
                    <table>
                        <thead>
                            <tr>
                                <th scope="col">Chip</th>
                                <th scope="col">Tidspunkt</th>
                                <th scope="col">Chip-temperatur</th>
                                <th scope="col">Batteri</th>
                            </tr>
                        </thead>
                        <tbody id="readings-body">
                            <tr>
                                <td class="table-message" colspan="4">Henter seneste målinger...</td>
                            </tr>
                        </tbody>
                    </table>
                </div>
            </section>
        </main>

        <footer class="site-footer">
            <span>AITSM Engineering ApS</span>
            <span>Opdateres automatisk</span>
        </footer>
    </div>
</body>
</html>
