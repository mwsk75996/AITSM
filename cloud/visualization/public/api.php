<?php

declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

/**
 * Afbryd med en JSON-fejl og den givne HTTP-statuskode.
 */
function reject(int $statusCode, string $message): never
{
    http_response_code($statusCode);
    echo json_encode(
        [
            'status' => 'error',
            'message' => $message,
        ],
        JSON_THROW_ON_ERROR
    );
    exit;
}

$pageSize = (int) ($_GET['page_size'] ?? $_GET['limit'] ?? 50);

$allowedPageSizes = [10, 25, 50, 100];
if (!in_array($pageSize, $allowedPageSizes, true)) {
    $pageSize = 50;
}

// Keyset-paginering: eksklusiv øvre grænse for timestamp (ISO-8601).
// Bruges i stedet for OFFSET, som ældre QuestDB-versioner ikke understøtter.
$cursor = null;
if (isset($_GET['cursor']) && $_GET['cursor'] !== '') {
    $rawCursor = (string) $_GET['cursor'];

    if (strlen($rawCursor) > 40 || !preg_match('/^[0-9T:\-.+Z ]+$/', $rawCursor)) {
        reject(400, 'Ugyldig cursor.');
    }

    $cursor = $rawCursor;
}

/**
 * Validér et ISO-8601-lignende tidsstempel (samme format som cursor).
 */
function validateTimestamp(string $value, string $parameter): string
{
    if (strlen($value) > 40 || !preg_match('/^[0-9T:\-.+Z ]+$/', $value)) {
        reject(400, "Ugyldig {$parameter}.");
    }

    return $value;
}

// Filtre. Alle er valgfrie, og uden dem opfører endpointet sig som før.
$date = null;
$from = null;
$to = null;
$deviceId = null;
$temperatureMin = null;
$temperatureMax = null;
$batteryMin = null;
$batteryMax = null;

if (isset($_GET['date']) && $_GET['date'] !== '') {
    $rawDate = (string) $_GET['date'];

    if (
        !preg_match('/^\d{4}-\d{2}-\d{2}$/', $rawDate)
        || !checkdate((int) substr($rawDate, 5, 2), (int) substr($rawDate, 8, 2), (int) substr($rawDate, 0, 4))
    ) {
        reject(400, 'Ugyldig date (forventet YYYY-MM-DD).');
    }

    if (isset($_GET['from']) || isset($_GET['to'])) {
        reject(400, 'Brug enten date eller from/to, ikke begge.');
    }

    $date = $rawDate;
    $day = new DateTimeImmutable($rawDate . ' 00:00:00', new DateTimeZone('UTC'));
    $from = $day->format('Y-m-d\TH:i:s.v\Z');
    $to = $day->modify('+1 day')->format('Y-m-d\TH:i:s.v\Z');
} else {
    if (isset($_GET['from']) && $_GET['from'] !== '') {
        $from = validateTimestamp((string) $_GET['from'], 'from');
    }

    if (isset($_GET['to']) && $_GET['to'] !== '') {
        $to = validateTimestamp((string) $_GET['to'], 'to');
    }
}

if (isset($_GET['device_id']) && $_GET['device_id'] !== '') {
    $rawDeviceId = (string) $_GET['device_id'];

    if (strlen($rawDeviceId) > 64 || !preg_match('/^[A-Za-z0-9._:-]+$/', $rawDeviceId)) {
        reject(400, 'Ugyldig device_id.');
    }

    $deviceId = $rawDeviceId;
}

/**
 * Validér en valgfri numerisk filterværdi.
 */
function validateNumber(mixed $value, string $parameter): ?float
{
    if ($value === null || $value === '') {
        return null;
    }

    if (!is_numeric($value)) {
        reject(400, "Ugyldig {$parameter} (forventet tal).");
    }

    return (float) $value;
}

$temperatureMin = validateNumber($_GET['temperature_min'] ?? null, 'temperature_min');
$temperatureMax = validateNumber($_GET['temperature_max'] ?? null, 'temperature_max');
$batteryMin = validateNumber($_GET['battery_min'] ?? null, 'battery_min');
$batteryMax = validateNumber($_GET['battery_max'] ?? null, 'battery_max');

$conditions = [];

if ($from !== null) {
    $conditions[] = "timestamp >= '{$from}'";
}

if ($to !== null) {
    $conditions[] = "timestamp < '{$to}'";
}

if ($deviceId !== null) {
    $conditions[] = "device_id = '{$deviceId}'";
}

if ($temperatureMin !== null) {
    $conditions[] = 'temperature >= ' . $temperatureMin;
}

if ($temperatureMax !== null) {
    $conditions[] = 'temperature <= ' . $temperatureMax;
}

if ($batteryMin !== null) {
    $conditions[] = 'battery >= ' . $batteryMin;
}

if ($batteryMax !== null) {
    $conditions[] = 'battery <= ' . $batteryMax;
}

$where = $conditions === [] ? '' : ' WHERE ' . implode(' AND ', $conditions);

$countQuery = "SELECT count(), avg(temperature), min(battery) FROM sensor_readings{$where}";

// Hent én række ekstra for at afgøre, om der findes en næste side.
$dataConditions = $conditions;

if ($cursor !== null) {
    $dataConditions[] = "timestamp < '{$cursor}'";
}

$dataWhere = $dataConditions === [] ? '' : ' WHERE ' . implode(' AND ', $dataConditions);

$dataQuery = sprintf(
    'SELECT timestamp, device_id, temperature, battery FROM sensor_readings%s ORDER BY timestamp DESC LIMIT %d',
    $dataWhere,
    $pageSize + 1
);

function questdbQuery(string $query): array
{
    $curl = curl_init('http://127.0.0.1:9000/exec?' . http_build_query(['query' => $query]));

    if ($curl === false) {
        throw new RuntimeException('Kunne ikke starte QuestDB-forbindelsen.');
    }

    curl_setopt_array($curl, [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_CONNECTTIMEOUT => 2,
        CURLOPT_TIMEOUT => 5,
    ]);

    $response = curl_exec($curl);
    $httpCode = (int) curl_getinfo($curl, CURLINFO_HTTP_CODE);
    $curlError = curl_error($curl);
    curl_close($curl);

    if ($response === false) {
        throw new RuntimeException($curlError ?: 'QuestDB returnerede ikke data.');
    }

    $decoded = json_decode($response, true);

    if (!is_array($decoded)) {
        throw new RuntimeException("QuestDB svarede HTTP {$httpCode} med et ugyldigt svar.");
    }

    if (isset($decoded['error'])) {
        error_log('QuestDB-fejl: ' . $decoded['error']);
        throw new RuntimeException('QuestDB afviste forespørgslen: ' . $decoded['error']);
    }

    if ($httpCode >= 400) {
        throw new RuntimeException("QuestDB svarede HTTP {$httpCode}.");
    }

    return $decoded;
}

try {
    $countPayload = questdbQuery($countQuery);
    $aggregateRow = $countPayload['dataset'][0] ?? [0, null, null];
    $total = (int) ($aggregateRow[0] ?? 0);

    $payload = questdbQuery($dataQuery);
    $columnNames = array_map(
        static fn (array $column): string => $column['name'],
        $payload['columns'] ?? []
    );

    $readings = [];
    foreach ($payload['dataset'] ?? [] as $row) {
        $values = array_combine($columnNames, $row);

        if ($values === false) {
            continue;
        }

        $readings[] = [
            'device_id' => $values['device_id'] ?? null,
            'timestamp' => $values['timestamp'] ?? null,
            'temperature' => $values['temperature'] ?? null,
            'battery' => $values['battery'] ?? null,
        ];
    }

    $hasMore = count($readings) > $pageSize;

    if ($hasMore) {
        $readings = array_slice($readings, 0, $pageSize);
    }

    $nextCursor = $hasMore ? ($readings[count($readings) - 1]['timestamp'] ?? null) : null;

    echo json_encode(
        [
            'status' => 'ok',
            'page_size' => $pageSize,
            'total' => $total,
            'has_more' => $hasMore,
            'next_cursor' => $nextCursor,
            'summary' => [
                'avg_temperature' => $aggregateRow[1] ?? null,
                'min_battery' => $aggregateRow[2] ?? null,
            ],
            'readings' => $readings,
        ],
        JSON_THROW_ON_ERROR
    );
} catch (Throwable $error) {
    http_response_code(502);

    echo json_encode(
        [
            'status' => 'error',
            'message' => 'Kunne ikke hente sensordata fra QuestDB.',
            'detail' => $error->getMessage(),
        ],
        JSON_THROW_ON_ERROR
    );
}
