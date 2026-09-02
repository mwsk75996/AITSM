const REFRESH_INTERVAL_MS = 30000;
const COLUMN_COUNT = 4;
const SKELETON_ITEMS = 4;
const VIEW_STORAGE_KEY = 'aitsm-visning';

const EYEBROW_MESSAGES = [
    'Live fra feltet!',
    'Varmt lige nu, eller er det bare mig?',
    'Sensorerne sover aldrig.',
    'Friskt fra Thingy:91 X!',
    'Held og lykke med at holde batteriet i live.',
    'Data så friske, de damper stadig.',
    '100% flere grader end forventet, måske.',
    'Ingen mennesker blev involveret i denne måling.',
    'Straight outta QuestDB.',
    'Bip. Bop. Måling modtaget.',
    'Kold kaffe, varme sensorer.',
    'Det her opdaterer sig selv - du behøver ikke.',
    'Endnu ikke sponsoreret af et termometer.',
    'Live og direkte, ligesom vejret.',
    'Batteriniveau: bekymrende optimistisk.',
    'Reload for endnu en tilfældig hilsen!',
    'Sensorerne rapporterer, vi bare videreformidler.',
    'Ingen skyer her, kun sky-data.',
    'Måler verden, ét device ad gangen.',
    'Held og lykke, må dine grader være stabile.',
];

const loadbar = document.querySelector('#loadbar');
const eyebrowMessage = document.querySelector('#eyebrow-message');
const statusBadge = document.querySelector('#status-badge');
const statusLabel = document.querySelector('#status-label');
const readingCount = document.querySelector('#reading-count');
const readingCountHint = document.querySelector('#reading-count-hint');
const avgTemperature = document.querySelector('#avg-temperature');
const minBattery = document.querySelector('#min-battery');
const minBatteryHint = document.querySelector('#min-battery-hint');
const lastUpdated = document.querySelector('#last-updated');
const chartCard = document.querySelector('#chart-card');
const deviceChart = document.querySelector('#device-chart');
const tableCard = document.querySelector('#table-card');
const readingsTable = document.querySelector('#readings-table');
const readingsBody = document.querySelector('#readings-body');
const viewChartButton = document.querySelector('#view-chart');
const viewTableButton = document.querySelector('#view-table');

function showRandomEyebrowMessage() {
    const message = EYEBROW_MESSAGES[Math.floor(Math.random() * EYEBROW_MESSAGES.length)];
    eyebrowMessage.textContent = message;
}

function setStatus(state, label) {
    statusBadge.dataset.state = state;
    statusLabel.textContent = label;
}

function setBusy(isBusy) {
    loadbar.dataset.loading = String(isBusy);
    chartCard.setAttribute('aria-busy', String(isBusy));
    readingsTable.setAttribute('aria-busy', String(isBusy));
}

function formatTimestamp(timestamp) {
    if (!timestamp) {
        return '—';
    }

    const date = new Date(timestamp);

    if (Number.isNaN(date.getTime())) {
        return timestamp;
    }

    return new Intl.DateTimeFormat('da-DK', {
        dateStyle: 'short',
        timeStyle: 'short',
    }).format(date);
}

function escapeHtml(value) {
    return String(value).replace(/[&<>"']/g, (character) => ({
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#039;',
    }[character]));
}

function toNumber(value) {
    if (value === null || value === undefined || value === '') {
        return null;
    }

    const number = Number(value);

    return Number.isFinite(number) ? number : null;
}

function formatNumber(value) {
    const number = toNumber(value);

    if (number === null) {
        return null;
    }

    return number.toLocaleString('da-DK', { maximumFractionDigits: 1 });
}

function formatValue(value, unit) {
    const formatted = formatNumber(value);

    return formatted === null ? '—' : `${formatted} ${unit}`;
}

function batteryLevel(value) {
    const number = toNumber(value);

    if (number === null) {
        return 'unknown';
    }

    if (number <= 10) {
        return 'critical';
    }

    if (number <= 30) {
        return 'low';
    }

    return 'ok';
}

function clampPercentage(value) {
    const number = toNumber(value);

    return number === null ? 0 : Math.min(100, Math.max(0, number));
}

/* Batteri */

function renderBattery(value, extraClass) {
    const label = escapeHtml(formatValue(value, '%'));

    return `
        <span class="batt ${extraClass}" data-level="${batteryLevel(value)}" role="img" aria-label="Batteri ${label}">
            <span class="batt-body">
                <span class="batt-fill" style="width: ${clampPercentage(value).toFixed(1)}%"></span>
            </span>
            <span class="batt-pct">${label}</span>
        </span>
    `;
}

/* Grafvisning */

function renderChartBar(reading, index, scaleMin, scaleMax) {
    const deviceId = escapeHtml(reading.device_id || 'Ukendt enhed');
    const temperature = toNumber(reading.temperature);
    const temperatureLabel = formatNumber(reading.temperature);
    const range = scaleMax - scaleMin || 1;
    const heightPct = temperature === null ? 0 : ((temperature - scaleMin) / range) * 100;
    const battLabel = escapeHtml(formatValue(reading.battery, '%'));

    return `
        <div class="chart-bar" style="--i: ${index}" data-color="${index % 6}">
            <span class="chart-bar-value">${temperatureLabel === null ? '—' : `${escapeHtml(temperatureLabel)}°`}</span>
            <span class="chart-bar-track" role="img" aria-label="${deviceId}: ${temperatureLabel === null ? 'ingen data' : `${escapeHtml(temperatureLabel)} grader`}">
                <span class="chart-bar-fill" style="height: ${Math.max(0, Math.min(100, heightPct)).toFixed(1)}%"></span>
            </span>
            <span class="chart-bar-batt" data-level="${batteryLevel(reading.battery)}" style="--batt: ${clampPercentage(reading.battery).toFixed(1)}%" role="img" aria-label="Batteri ${battLabel}"></span>
            <span class="chart-bar-label">${deviceId}</span>
        </div>
    `;
}

function renderChart(readings) {
    const temperatures = readings.map((reading) => toNumber(reading.temperature)).filter((value) => value !== null);
    const maxTemperature = temperatures.length === 0 ? 1 : Math.max(...temperatures);
    const minTemperature = temperatures.length === 0 ? 0 : Math.min(0, ...temperatures);
    const scaleMax = maxTemperature <= minTemperature ? minTemperature + 1 : maxTemperature * 1.15;

    deviceChart.innerHTML = readings
        .map((reading, index) => renderChartBar(reading, index, minTemperature, scaleMax))
        .join('');
}

/* Tabelrækker */

function renderTableRow(reading) {
    return `
        <tr role="row">
            <th role="rowheader" scope="row">${escapeHtml(reading.device_id || 'Ukendt enhed')}</th>
            <td role="cell" class="cell-time" data-label="Tidspunkt">${escapeHtml(formatTimestamp(reading.timestamp))}</td>
            <td role="cell" class="cell-numeric metric" data-label="Enhedstemperatur">${escapeHtml(formatValue(reading.temperature, '°C'))}</td>
            <td role="cell" class="cell-numeric" data-label="Batteri">${renderBattery(reading.battery, '')}</td>
        </tr>
    `;
}

/* Tilstande */

function renderState(emoji, title, text, tone) {
    const block = `
        <span class="state-emoji" aria-hidden="true">${emoji}</span>
        <span class="state-title">${escapeHtml(title)}</span>
        <span class="state-text">${escapeHtml(text)}</span>
    `;

    deviceChart.innerHTML = `<div class="state" data-tone="${tone}">${block}</div>`;
    readingsBody.innerHTML = `
        <tr role="row">
            <td role="cell" class="state-cell" data-tone="${tone}" colspan="${COLUMN_COUNT}">${block}</td>
        </tr>
    `;
}

function renderSkeleton() {
    deviceChart.innerHTML = Array.from({ length: SKELETON_ITEMS }, (_, index) => `
        <div class="chart-bar" style="--i: ${index}" aria-hidden="true">
            <span class="skeleton skeleton-line skeleton-narrow"></span>
            <span class="chart-bar-track">
                <span class="skeleton skeleton-bar-fill" style="height: ${30 + ((index * 17) % 55)}%"></span>
            </span>
            <span class="skeleton skeleton-line skeleton-batt-bar"></span>
            <span class="skeleton skeleton-line skeleton-wide"></span>
        </div>
    `).join('');

    readingsBody.innerHTML = Array.from({ length: SKELETON_ITEMS }, () => `
        <tr role="row" aria-hidden="true">
            <th role="rowheader" scope="row"><span class="skeleton skeleton-line skeleton-wide"></span></th>
            <td role="cell"><span class="skeleton skeleton-line skeleton-wide"></span></td>
            <td role="cell" class="cell-numeric"><span class="skeleton skeleton-line skeleton-narrow"></span></td>
            <td role="cell" class="cell-numeric"><span class="skeleton skeleton-batt"></span></td>
        </tr>
    `).join('');
}

/* Nøgletal */

function renderSummary(readings) {
    readingCount.textContent = readings.length.toLocaleString('da-DK');
    readingCountHint.textContent = readings.length === 1
        ? 'enhed rapporterer data'
        : 'enheder rapporterer data';

    const temperatures = readings.map((reading) => toNumber(reading.temperature)).filter((value) => value !== null);
    const batteries = readings.map((reading) => toNumber(reading.battery)).filter((value) => value !== null);

    if (temperatures.length === 0) {
        avgTemperature.textContent = '—';
    } else {
        const average = temperatures.reduce((sum, value) => sum + value, 0) / temperatures.length;
        avgTemperature.textContent = formatValue(average, '°C');
    }

    if (batteries.length === 0) {
        minBattery.textContent = '—';
        minBatteryHint.textContent = 'Ingen batteridata';
    } else {
        const lowest = Math.min(...batteries);
        minBattery.textContent = formatValue(lowest, '%');
        minBatteryHint.textContent = lowest <= 30 ? 'Enhed bør oplades' : 'Alle enheder har strøm nok';
    }
}

function renderReadings(readings) {
    renderSummary(readings);

    if (readings.length === 0) {
        renderState('👀', 'Ingen målinger fundet!', 'Der er endnu ikke registreret data fra nogen enhed.', 'empty');
        return;
    }

    renderChart(readings);
    readingsBody.innerHTML = readings.map(renderTableRow).join('');
}

/* Visningsskifter - graf og tabel vises aldrig samtidig */

function setView(view) {
    const showTable = view === 'table';

    tableCard.hidden = !showTable;
    chartCard.hidden = showTable;
    viewChartButton.setAttribute('aria-pressed', String(!showTable));
    viewTableButton.setAttribute('aria-pressed', String(showTable));

    try {
        localStorage.setItem(VIEW_STORAGE_KEY, showTable ? 'table' : 'chart');
    } catch (error) {
        /* localStorage kan være blokeret - visningen virker stadig. */
    }
}

function restoreView() {
    try {
        setView(localStorage.getItem(VIEW_STORAGE_KEY) === 'table' ? 'table' : 'chart');
    } catch (error) {
        setView('chart');
    }
}

viewChartButton.addEventListener('click', () => setView('chart'));
viewTableButton.addEventListener('click', () => setView('table'));

/* Datahentning */

async function loadReadings() {
    setBusy(true);

    try {
        const response = await fetch('api.php', {
            cache: 'no-store',
            headers: { Accept: 'application/json' },
        });

        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        const data = await response.json();

        if (data.status !== 'ok' || !Array.isArray(data.readings)) {
            throw new Error('Ugyldigt API-svar');
        }

        renderReadings(data.readings);

        const time = new Intl.DateTimeFormat('da-DK', {
            hour: '2-digit',
            minute: '2-digit',
        }).format(new Date());

        lastUpdated.textContent = time;
        lastUpdated.setAttribute('aria-label', `Opdateret ${time}`);
        setStatus('online', 'Online');
    } catch (error) {
        readingCount.textContent = '—';
        readingCountHint.textContent = 'Kunne ikke hente data';
        avgTemperature.textContent = '—';
        minBattery.textContent = '—';
        minBatteryHint.textContent = 'Ingen batteridata';
        renderState(
            '💥',
            'QuestDB-data er midlertidigt utilgængelige!',
            'Vi prøver igen om lidt – siden opdaterer sig selv.',
            'error'
        );
        setStatus('offline', 'Offline');
        console.error('Kunne ikke hente sensordata:', error);
    } finally {
        setBusy(false);
    }
}

showRandomEyebrowMessage();
restoreView();
renderSkeleton();
loadReadings();
setInterval(loadReadings, REFRESH_INTERVAL_MS);
