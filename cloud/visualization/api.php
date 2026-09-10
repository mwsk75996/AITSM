<?php

declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

$page = (int) ($_GET['page'] ?? 1);
$pageSize = (int) ($_GET['page_size'] ?? $_GET['limit'] ?? 50);

if ($page < 1) {
    $page = 1;
}

$allowedPageSizes = [10, 25, 50, 100];
if (!in_array($pageSize, $allowedPageSizes, true)) {
    $pageSize = 50;
}

$offset = ($page - 1) * $pageSize;

$countQuery = 'SELECT count(), avg(temperature), min(battery) FROM sensor_readings';
$dataQuery = sprintf(
    'SELECT timestamp, device_id, temperature, battery FROM sensor_readings ORDER BY timestamp DESC LIMIT %d OFFSET %d',
    $pageSize,
    $offset
);

function questdbQuery(string $query): array
{
    $curl = curl_init('http://127.0.0.1:9000/exec?' . http_build_query(['query' => $query]));

    if ($curl === false) {
        throw new RuntimeException('Kunne ikke starte QuestDB-forbindelsen.');
    }

    curl_setopt_array($curl, [
        CURLOPT_FAILONERROR => true,
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_CONNECTTIMEOUT => 2,
        CURLOPT_TIMEOUT => 5,
    ]);

    $response = curl_exec($curl);
    $curlError = curl_error($curl);
    curl_close($curl);

    if ($response === false) {
        throw new RuntimeException($curlError ?: 'QuestDB returnerede ikke data.');
    }

    return json_decode($response, true, 512, JSON_THROW_ON_ERROR);
}

try {
    $countPayload = questdbQuery($countQuery);
    $aggregateRow = $countPayload['dataset'][0] ?? [0, null, null];
    $total = (int) ($aggregateRow[0] ?? 0);

    $totalPages = $total > 0 ? (int) ceil($total / $pageSize) : 1;
    if ($page > $totalPages) {
        $page = $totalPages;
        $offset = ($page - 1) * $pageSize;
        $dataQuery = sprintf(
            'SELECT timestamp, device_id, temperature, battery FROM sensor_readings ORDER BY timestamp DESC LIMIT %d OFFSET %d',
            $pageSize,
            $offset
        );
    }

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

    echo json_encode(
        [
            'status' => 'ok',
            'page' => $page,
            'page_size' => $pageSize,
            'total' => $total,
            'total_pages' => $totalPages,
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
        ],
        JSON_THROW_ON_ERROR
    );
}
