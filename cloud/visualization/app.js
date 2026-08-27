const apiStatus = document.querySelector('#api-status');
const apiState = document.querySelector('#api-state');
const apiMessage = document.querySelector('#api-message');
const lastUpdated = document.querySelector('#last-updated');
const year = document.querySelector('#year');

year.textContent = new Date().getFullYear();

function showApiState(state, label, message, updatedAt = null) {
    apiStatus.dataset.state = state;
    apiStatus.lastChild.textContent = ` ${label}`;
    apiState.textContent = label;
    apiMessage.textContent = message;

    if (updatedAt) {
        lastUpdated.textContent = new Date(updatedAt).toLocaleTimeString('da-DK', {
            hour: '2-digit',
            minute: '2-digit',
        });
    }
}

async function loadApiStatus() {
    try {
        const response = await fetch('api.php', { headers: { Accept: 'application/json' } });

        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        const data = await response.json();
        showApiState('online', 'Online', data.message, data.updated_at);
    } catch (error) {
        showApiState('offline', 'Offline', 'Kunne ikke kontakte visualization API.');
        console.error('Visualization API-fejl:', error);
    }
}

loadApiStatus();
