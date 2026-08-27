const statusDot = document.querySelector('#status-dot');
const statusLabel = document.querySelector('#status-label');
const readingCount = document.querySelector('#reading-count');
const lastUpdated = document.querySelector('#last-updated');
const readingsBody = document.querySelector('#readings-body');

function setStatus(state, label) {
    statusDot.dataset.state = state;
    statusLabel.textContent = label;
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

function formatValue(value, unit) {
    if (value === null || value === undefined || value === '') {
        return '—';
    }

    const number = Number(value);

    if (!Number.isFinite(number)) {
        return '—';
    }

    return `${number.toLocaleString('da-DK', {
        maximumFractionDigits: 1,
    })} ${unit}`;
}

function renderReadings(readings) {
    readingCount.textContent = `${readings.length} ${readings.length === 1 ? 'enhed' : 'enheder'}`;

    if (readings.length === 0) {
        readingsBody.innerHTML = '<tr><td class="table-message" colspan="4">Ingen målinger fundet.</td></tr>';
        return;
    }

    readingsBody.innerHTML = readings.map((reading) => `
        <tr>
            <th scope="row">${escapeHtml(reading.device_id || 'Ukendt enhed')}</th>
            <td>${escapeHtml(formatTimestamp(reading.timestamp))}</td>
            <td>${escapeHtml(formatValue(reading.temperature, '°C'))}</td>
            <td>${escapeHtml(formatValue(reading.battery, '%'))}</td>
        </tr>
    `).join('');
}

async function loadReadings() {
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
        lastUpdated.textContent = `Opdateret ${new Intl.DateTimeFormat('da-DK', {
            hour: '2-digit',
            minute: '2-digit',
        }).format(new Date())}`;
        setStatus('online', 'Online');
    } catch (error) {
        readingCount.textContent = 'Kunne ikke hente data';
        readingsBody.innerHTML = '<tr><td class="table-message" colspan="4">QuestDB-data er midlertidigt utilgængelige.</td></tr>';
        setStatus('offline', 'Offline');
        console.error('Kunne ikke hente sensordata:', error);
    }
}

loadReadings();
setInterval(loadReadings, 30000);
