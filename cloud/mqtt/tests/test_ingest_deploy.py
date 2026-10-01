from datetime import datetime, timezone
import subprocess
import sys
import pytest
import deploy_ingest as deploy
import verify_ingest_outage as outage


def test_atomic_replace_preserves_permissions(tmp_path):
    target = tmp_path / "ingest.py"
    target.write_bytes(b"old")
    target.chmod(0o640)
    before = target.stat()
    deploy.replace_script(target, b"new", before)
    assert target.read_bytes() == b"new"
    assert target.stat().st_mode == before.st_mode
    assert target.stat().st_uid == before.st_uid
    assert list(tmp_path.iterdir()) == [target]


def test_failed_subscription_rolls_back_and_restarts_original(tmp_path, monkeypatch):
    target = tmp_path / "old" / "ingest.py"
    target.parent.mkdir()
    target.write_text("# original\n")
    source = tmp_path / "deploy_ingest.py"
    source.with_name("ingest.py").write_text("# replacement\n")
    monkeypatch.setattr(deploy, "__file__", str(source))
    monkeypatch.setattr(deploy, "service_command", lambda: ("python", target))
    commands = []
    monkeypatch.setattr(deploy.subprocess, "run", lambda command, **kwargs: commands.append(command))
    def fail(since):
        assert target.read_text() == "# replacement\n"
        raise RuntimeError("Ingen subscription")
    monkeypatch.setattr(deploy, "wait_for_subscription", fail)
    with pytest.raises(RuntimeError, match="Ingen subscription"):
        deploy.deploy()
    assert target.read_text() == "# original\n"
    assert commands.count(["systemctl", "restart", deploy.SERVICE]) == 2


def test_paho_check_accepts_installed_paho_with_manual_ack():
    result = subprocess.run([sys.executable, "-c", deploy.PAHO_CHECK],
                            capture_output=True, text=True, check=True)
    assert "understøtter manuel ACK" in result.stdout


def test_paho_without_manual_ack_stops_deploy_before_replacement(tmp_path, monkeypatch):
    target = tmp_path / "old" / "ingest.py"
    target.parent.mkdir()
    target.write_text("# original\n")
    source = tmp_path / "ingest.py"
    source.write_text("# replacement\n")
    monkeypatch.setattr(deploy, "service_command", lambda: ("python", target))
    commands = []
    def run(command, **kwargs):
        commands.append(command)
        if command[1:] == ["-c", deploy.PAHO_CHECK]:
            raise subprocess.CalledProcessError(1, command)
    monkeypatch.setattr(deploy.subprocess, "run", run)
    with pytest.raises(subprocess.CalledProcessError):
        deploy.deploy(source)
    assert target.read_text() == "# original\n"
    assert commands == [["python", "-c", deploy.PAHO_CHECK]]


def test_identical_script_does_not_restart_service(tmp_path, monkeypatch):
    target = tmp_path / "ingest.py"
    target.write_text("# identical\n")
    monkeypatch.setattr(deploy, "__file__", str(tmp_path / "deploy_ingest.py"))
    monkeypatch.setattr(deploy, "service_command", lambda: ("python", target))
    commands = []
    monkeypatch.setattr(deploy.subprocess, "run", lambda command, **kwargs: commands.append(command))
    deploy.deploy()
    assert ["systemctl", "is-active", "--quiet", deploy.SERVICE] in commands
    assert not any(command[1] == "restart" for command in commands)


def outage_setup(monkeypatch, query):
    commands = []
    monkeypatch.setattr(outage.subprocess, "run", lambda command, **kwargs: commands.append(command))
    monkeypatch.setattr(outage, "broker_limits", lambda: None)
    monkeypatch.setattr(outage.time, "sleep", lambda delay: None)
    monkeypatch.setattr(outage.schema, "query", query)
    return commands


def test_outage_query_failure_still_starts_ingest(monkeypatch):
    def query(sql):
        if "LIMIT 1" in sql:
            return [{"timestamp": datetime.now(timezone.utc).isoformat()}]
        raise RuntimeError("Database utilgængelig")
    commands = outage_setup(monkeypatch, query)
    with pytest.raises(RuntimeError, match="Database utilgængelig"):
        outage.verify()
    assert ["systemctl", "stop", outage.SERVICE] in commands
    assert commands[-2:] == [["systemctl", "start", outage.SERVICE],
                             ["systemctl", "stop", outage.RESTORE_UNIT + ".timer"]]
    assert commands[1][0] == "systemd-run", "Guard must be armed before stopping ingest"


def test_outage_requires_recent_device_data_before_stopping(monkeypatch):
    commands = outage_setup(monkeypatch, lambda sql: [])
    with pytest.raises(RuntimeError, match="aktiv Thingy"):
        outage.verify()
    assert not any(command[1] == "stop" for command in commands)


def test_outage_checks_recovered_batch_cadence(monkeypatch):
    base = 1790143200
    def stamp(t):
        return datetime.fromtimestamp(t, timezone.utc).isoformat()
    rows = [{"timestamp": stamp(base + (i + 1) * 15), "temperature": 23.0, "battery": 98.0} for i in range(20)]
    answers = iter([[{"timestamp": stamp(base)}], [], rows])
    moments = iter([base + 10, base + 10, base + 370])
    commands = outage_setup(monkeypatch, lambda sql: next(answers))
    monkeypatch.setattr(outage.time, "time", lambda: next(moments))
    outage.verify()
    assert commands[-1] == ["systemctl", "is-active", "--quiet", outage.SERVICE]


def test_failed_restart_keeps_independent_restore_timer_armed(monkeypatch):
    commands = []
    def run(command, **kwargs):
        commands.append(command)
        if command == ["systemctl", "start", outage.SERVICE]:
            raise RuntimeError("Genstart fejlede")
    monkeypatch.setattr(outage.subprocess, "run", run)
    with pytest.raises(RuntimeError, match="Genstart fejlede"):
        with outage.restore_ingest():
            raise OSError("Forbindelse mistet")
    assert commands[0][0] == "systemd-run"
    assert ["systemctl", "stop", outage.RESTORE_UNIT + ".timer"] not in commands


def test_termination_signal_runs_restore_cleanup(monkeypatch):
    commands = []
    monkeypatch.setattr(outage.subprocess, "run", lambda command, **kwargs: commands.append(command))
    with pytest.raises(InterruptedError):
        with outage.restore_ingest():
            outage.signal.getsignal(outage.signal.SIGTERM)(outage.signal.SIGTERM, None)
    assert commands[-2:] == [["systemctl", "start", outage.SERVICE],
                             ["systemctl", "stop", outage.RESTORE_UNIT + ".timer"]]


def test_broker_limits_reject_per_listener_settings(tmp_path, capsys):
    (tmp_path / "conf.d").mkdir()
    (tmp_path / "mosquitto.conf").write_text("persistence true\npassword_file /etc/mosquitto/passwd\n")
    (tmp_path / "conf.d" / "aitsm.conf").write_text("per_listener_settings false\n")
    outage.broker_limits(tmp_path)
    assert capsys.readouterr().out == "Mosquitto persistence=true\nMosquitto per_listener_settings=false\n"
    (tmp_path / "conf.d" / "aitsm.conf").write_text("per_listener_settings true\n")
    with pytest.raises(RuntimeError, match="per_listener_settings"):
        outage.broker_limits(tmp_path)
