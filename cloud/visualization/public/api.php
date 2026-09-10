<?php

declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

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

    if (strlen($rawCursor) > 40 || !preg_match('/^[0-9T:\\-.+Z ]+$/', $rawCursor)) {
        http_response_code(400);
        echo json_encode(
            [
                'status' => 'error',
                'message' => 'Ugyldig cursor.',
            ],
            JSON_THROW_ON_ERROR
        );
        exit;
    }

    $cursor = $rawCursor;
}

$countQuery = 'SELECT count(), avg(temperature), min(battery) FROM sensor_readings';

// Hent én række ekstra for at afgøre, om der findes en næste side.
$dataQuery = sprintf(
    "SELECT timestamp, device_id, temperature, battery FROM sensor_readings%s ORDER BY timestamp DESC LIMIT %d",
    $cursor === null ? '' : " WHERE timestamp < '" . $cursor . "'",
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
