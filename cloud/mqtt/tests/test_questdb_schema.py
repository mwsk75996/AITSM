import io
import json
from urllib.parse import parse_qs, urlparse
import pytest
import questdb_schema as schema


def test_query_uses_url_encoding_and_surfaces_sql_errors(monkeypatch):
    def response(url, timeout):
        assert parse_qs(urlparse(url).query)["query"] == ["SELECT 'x & y'"]
        assert timeout == 20
        return io.StringIO(json.dumps({"error": "bad schema"}))
    monkeypatch.setattr(schema, "urlopen", response)
    with pytest.raises(RuntimeError, match="bad schema"):
        schema.query("SELECT 'x & y'")


def test_query_maps_columns_to_rows(monkeypatch):
    monkeypatch.setattr(schema, "urlopen", lambda *a, **k: io.StringIO(json.dumps(
        {"columns": [{"name": "dedup"}], "dataset": [[True]]})))
    assert schema.query("SELECT dedup") == [{"dedup": True}]


def backend(monkeypatch, *, wal=True, duplicates=0, columns=None, apply_keys=True):
    monkeypatch.setattr(schema.time, "sleep", lambda delay: None)
    calls = []
    migrated = False
    names = columns or ["timestamp", "device_id", "temperature", "battery"]
    def query(sql, url):
        nonlocal migrated
        calls.append(sql)
        if sql.startswith("ALTER TABLE"):
            migrated = True
            return []
        if "table_columns" in sql:
            return [{"column": name, "upsertKey": migrated and apply_keys and name in
                     ("timestamp", "device_id")} for name in names]
        if "duplicates" in sql:
            return [{"duplicates": duplicates}]
        return [{"walEnabled": wal, "designatedTimestamp": "timestamp", "dedup": migrated}]
    monkeypatch.setattr(schema, "query", query)
    return calls


def test_migration_verifies_actual_key_configuration(monkeypatch):
    calls = backend(monkeypatch)
    schema.enable_dedup()
    assert any(s.startswith("ALTER TABLE") for s in calls)
    assert "table_columns" in calls[-1]


@pytest.mark.parametrize("options, reason", [
    ({"wal": False}, "WAL"),
    ({"duplicates": 1}, "dublet"),
    ({"columns": ["timestamp", "device_id", "temperature", "unrelated"]}, "kolonner"),
])
def test_preflight_failure_does_not_mutate_database(monkeypatch, options, reason):
    calls = backend(monkeypatch, **options)
    with pytest.raises(RuntimeError, match=reason):
        schema.enable_dedup()
    assert not any(s.startswith("ALTER TABLE") for s in calls)


def test_migration_fails_if_server_does_not_enable_keys(monkeypatch):
    backend(monkeypatch, apply_keys=False)
    with pytest.raises(RuntimeError, match="nøgler"):
        schema.enable_dedup()
