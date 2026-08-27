<?php

declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

$query = <<<'SQL'
SELECT timestamp, device_id, temperature, battery
FROM sensor_readings
LATEST ON timestamp PARTITION BY device_id;
SQL;

try {
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

    $payload = json_decode($response, true, 512, JSON_THROW_ON_ERROR);
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
