ALTER TABLE sensor_readings DEDUP ENABLE UPSERT KEYS(timestamp, device_id);
