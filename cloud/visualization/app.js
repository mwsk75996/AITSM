const REFRESH_INTERVAL_MS = 30000;
const COLUMN_COUNT = 4;
const SKELETON_ITEMS = 4;

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
const readingsTable = document.querySelector('#readings-table');
const readingsBody = document.querySelector('#readings-body');
const paginationInfo = document.querySelector('#pagination-info');
const pageButtons = document.querySelector('#page-buttons');
const pageSizeSelect = document.querySelector('#page-size');

const state = {
    pageSize: Number(pageSizeSelect?.value) || 50,
    total: 0,
    // Keyset-paginering: cursors[0] er altid null (første side).
    cursors: [null],
    pageIndex: 0,
    nextCursor: null,
    hasMore: false,
};

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

    readingsBody.innerHTML = `
        <tr role="row">
            <td role="cell" class="state-cell" data-tone="${tone}" colspan="${COLUMN_COUNT}">${block}</td>
        </tr>
    `;
}

function renderSkeleton() {
    readingsBody.innerHTML = Array.from({ length: SKELETON_ITEMS }, () => `
        <tr role="row" aria-hidden="true">
            <th role="rowheader" scope="row"><span class="skeleton skeleton-line skeleton-wide"></span></th>
            <td role="cell"><span class="skeleton skeleton-line skeleton-wide"></span></td>
            <td role="cell" class="cell-numeric"><span class="skeleton skeleton-line skeleton-narrow"></span></td>
            <td role="cell" class="cell-numeric"><span class="skeleton skeleton-batt"></span></td>
        </tr>
    `).join('');
}

/* Nøgletal (globale aggregater fra serveren, så de dækker hele historikken) */

function renderSummary(summary) {
    const total = state.total;

    readingCount.textContent = total.toLocaleString('da-DK');
    readingCountHint.textContent = total === 1
        ? 'måling i historikken'
        : 'målinger i historikken';

    const avgTemperatureValue = toNumber(summary?.avg_temperature);
    avgTemperature.textContent = avgTemperatureValue === null
        ? '—'
        : formatValue(avgTemperatureValue, '°C');

    const minBatteryValue = toNumber(summary?.min_battery);
    if (minBatteryValue === null) {
        minBattery.textContent = '—';
        minBatteryHint.textContent = 'Ingen batteridata';
    } else {
        minBattery.textContent = formatValue(minBatteryValue, '%');
        minBatteryHint.textContent = minBatteryValue <= 30 ? 'Enhed bør oplades' : 'Alle enheder har strøm nok';
    }
}

/* Paginering (keyset: Forrige/Næste via cursor, ingen OFFSET) */

function renderPagination() {
    if (!pageButtons || !paginationInfo) {
        return;
    }

    const { pageIndex, pageSize, total, hasMore } = state;

    pageButtons.innerHTML = '';

    const prevButton = document.createElement('button');
    prevButton.type = 'button';
    prevButton.className = 'page-btn';
    prevButton.textContent = '‹ Forrige';
    prevButton.disabled = pageIndex <= 0;
    prevButton.setAttribute('aria-label', 'Forrige side');
    prevButton.addEventListener('click', () => loadReadings(pageIndex - 1));
    pageButtons.appendChild(prevButton);

    const nextButton = document.createElement('button');
    nextButton.type = 'button';
    nextButton.className = 'page-btn';
    nextButton.textContent = 'Næste ›';
    nextButton.disabled = !hasMore;
    nextButton.setAttribute('aria-label', 'Næste side');
    nextButton.addEventListener('click', () => loadReadings(pageIndex + 1));
    pageButtons.appendChild(nextButton);

    if (total === 0) {
        paginationInfo.textContent = 'Ingen målinger';
    } else {
        const from = pageIndex * pageSize + 1;
        const to = Math.min((pageIndex + 1) * pageSize, total);
        paginationInfo.textContent = `Viser ${from.toLocaleString('da-DK')}–${to.toLocaleString('da-DK')} af ${total.toLocaleString('da-DK')}`;
    }
}

function renderReadings(readings, summary) {
    renderSummary(summary);
    renderPagination();

    if (readings.length === 0) {
        renderState('👀', 'Ingen målinger fundet!', 'Der er endnu ikke registreret data fra nogen enhed.', 'empty');
        return;
    }

    readingsBody.innerHTML = readings.map(renderTableRow).join('');
}

/* Datahentning */

async function loadReadings(requestedIndex = state.pageIndex) {
    setBusy(true);

    const pageIndex = Math.max(0, requestedIndex);
    const cursor = state.cursors[pageIndex] ?? null;

    try {
        const params = new URLSearchParams({ page_size: String(state.pageSize) });

        if (cursor !== null) {
            params.set('cursor', cursor);
        }

        const response = await fetch(`api.php?${params.toString()}`, {
            cache: 'no-store',
            headers: { Accept: 'application/json' },
        });

        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        const data = await response.json();

        if (data.status !== 'ok' || !Array.isArray(data.readings)) {
            throw new Error(data.detail || 'Ugyldigt API-svar');
        }

        state.pageIndex = pageIndex;
        state.pageSize = Number(data.page_size) || state.pageSize;
        state.total = Number(data.total) || 0;
        state.hasMore = data.has_more === true;
        state.nextCursor = typeof data.next_cursor === 'string' ? data.next_cursor : null;

        if (state.hasMore) {
            state.cursors[pageIndex + 1] = state.nextCursor;
        } else {
            state.cursors.length = pageIndex + 1;
        }

        if (pageSizeSelect && pageSizeSelect.value !== String(state.pageSize)) {
            pageSizeSelect.value = String(state.pageSize);
        }

        renderReadings(data.readings, data.summary);

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
        if (paginationInfo) {
            paginationInfo.textContent = '—';
        }
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

pageSizeSelect?.addEventListener('change', () => {
    state.pageSize = Number(pageSizeSelect.value) || 50;
    state.cursors = [null];
    state.pageIndex = 0;
    state.nextCursor = null;
    state.hasMore = false;
    loadReadings(0);
});

showRandomEyebrowMessage();
renderSkeleton();
loadReadings(0);
setInterval(() => loadReadings(), REFRESH_INTERVAL_MS);
