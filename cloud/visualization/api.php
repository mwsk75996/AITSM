<?php

declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');

echo json_encode(
    [
        'status' => 'ok',
        'service' => 'visualization',
        'message' => 'Visualization API er online.',
        'updated_at' => gmdate(DATE_ATOM),
    ],
    JSON_THROW_ON_ERROR
);
