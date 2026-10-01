import json
import math
from datetime import datetime, timezone
from unittest.mock import Mock

import pytest

import ingest
from paho.mqtt.packettypes import PacketTypes

TOPIC = "aitsm/thingy91x/telemetry"
# 2026-09-23T06:00:00Z
UNIX_SECONDS = 1_790_143_200
UNIX_NS = UNIX_SECONDS * 1_000_000_000


class TestParseTimestamp:
    def test_missing_timestamp_uses_current_time(self):
        before = datetime.now(timezone.utc)
        parsed = ingest.parse_timestamp(None)
        after = datetime.now(timezone.utc)
        assert before <= parsed <= after

    def test_unix_seconds(self):
        assert ingest.parse_timestamp(UNIX_SECONDS) == datetime(2026, 9, 23, 6, tzinfo=timezone.utc)

    def test_unix_milliseconds(self):
        assert ingest.parse_timestamp(UNIX_SECONDS * 1000) == ingest.parse_timestamp(UNIX_SECONDS)

    def test_iso_with_z_suffix(self):
        assert ingest.parse_timestamp("2026-09-23T06:00:00Z") == ingest.parse_timestamp(UNIX_SECONDS)

    def test_iso_with_offset_is_converted_to_utc(self):
        assert ingest.parse_timestamp("2026-09-23T08:00:00+02:00") == ingest.parse_timestamp(UNIX_SECONDS)

    def test_iso_without_timezone_is_treated_as_utc(self):
        assert ingest.parse_timestamp("2026-09-23T06:00:00") == ingest.parse_timestamp(UNIX_SECONDS)

    @pytest.mark.parametrize("value", [True, [], {}])
    def test_unsupported_types_are_rejected(self, value):
        with pytest.raises(ValueError):
            ingest.parse_timestamp(value)

    def test_invalid_iso_string_is_rejected(self):
        with pytest.raises(ValueError):
            ingest.parse_timestamp("ikke en dato")


class TestFiniteNumber:
    @pytest.mark.parametrize("value", [None, True, False, math.nan, math.inf, -math.inf])
    def test_missing_or_non_finite_values_are_skipped(self, value):
        assert ingest.finite_number(value) is None

    @pytest.mark.parametrize("value, expected", [(23, 23.0), (-1.25, -1.25), ("98.76", 98.76)])
    def test_numbers_are_converted_to_float(self, value, expected):
        assert ingest.finite_number(value) == expected

    def test_non_numeric_string_is_rejected(self):
        with pytest.raises(ValueError):
            ingest.finite_number("varm")


def test_escape_tag_escapes_line_protocol_characters():
    assert ingest.escape_tag(r"a b,c=d\e") == r"a\ b\,c\=d\\e"


class TestBuildLine:
    def test_flat_payload(self):
        payload = {"device_id": "thingy91x", "timestamp": UNIX_SECONDS, "temperature": 23.45, "battery": 98.76}
        assert ingest.build_line(payload, TOPIC) == (
            f"sensor_readings,device_id=thingy91x temperature=23.45,battery=98.76 {UNIX_NS}\n"
        )

    def test_device_id_falls_back_to_topic(self):
        line = ingest.build_line({"timestamp": UNIX_SECONDS, "battery": 50}, "aitsm/andet-device/telemetry")
        assert line.startswith("sensor_readings,device_id=andet-device ")

    def test_device_id_falls_back_to_unknown_for_empty_topic_segment(self):
        line = ingest.build_line({"timestamp": UNIX_SECONDS, "battery": 50}, "aitsm//telemetry")
        assert line.startswith("sensor_readings,device_id=unknown ")

    def test_camel_case_device_id(self):
        line = ingest.build_line({"deviceId": "dev 1", "timestamp": UNIX_SECONDS, "battery": 50}, TOPIC)
        assert line.startswith(r"sensor_readings,device_id=dev\ 1 ")

    def test_values_object_and_chip_temperature(self):
        payload = {"timestamp": UNIX_SECONDS, "values": {"chip_temperature": 30.5, "temperature": 99}}
        assert ingest.build_line(payload, TOPIC) == (
            f"sensor_readings,device_id=thingy91x temperature=30.5 {UNIX_NS}\n"
        )

    def test_unknown_fields_are_ignored(self):
        payload = {"timestamp": UNIX_SECONDS, "battery": 80, "humidity": 40}
        assert "humidity" not in ingest.build_line(payload, TOPIC)

    def test_non_finite_value_is_left_out(self):
        payload = {"timestamp": UNIX_SECONDS, "temperature": math.nan, "battery": 80}
        assert ingest.build_line(payload, TOPIC) == (
            f"sensor_readings,device_id=thingy91x battery=80.0 {UNIX_NS}\n"
        )

    @pytest.mark.parametrize(
        "payload",
        [
            {"timestamp": UNIX_SECONDS},
            {"timestamp": UNIX_SECONDS, "humidity": 40},
            {"timestamp": UNIX_SECONDS, "temperature": None, "battery": None},
        ],
    )
    def test_payload_without_known_values_is_rejected(self, payload):
        with pytest.raises(ValueError, match="ingen kendte"):
            ingest.build_line(payload, TOPIC)

    @pytest.mark.parametrize("device_id", ["   ", 42])
    def test_invalid_device_id_is_rejected(self, device_id):
        with pytest.raises(ValueError, match="device_id"):
            ingest.build_line({"device_id": device_id, "battery": 50}, TOPIC)

    def test_values_must_be_an_object(self):
        with pytest.raises(ValueError, match="values"):
            ingest.build_line({"values": [1, 2]}, TOPIC)

    def test_invalid_timestamp_is_rejected(self):
        with pytest.raises(ValueError):
            ingest.build_line({"timestamp": "i går", "battery": 50}, TOPIC)


class TestBuildLines:
    def test_single_payload_gives_one_line(self):
        lines = ingest.build_lines({"timestamp": UNIX_SECONDS, "battery": 50}, TOPIC)
        assert len(lines) == 1

    def test_batch_gives_one_line_per_reading(self):
        payload = {
            "device_id": "thingy91x",
            "readings": [
                {"timestamp": UNIX_SECONDS, "temperature": 23.45, "battery": 98.76},
                {"timestamp": UNIX_SECONDS + 15, "temperature": -1.25, "battery": 98.0},
            ],
        }
        assert ingest.build_lines(payload, TOPIC) == [
            f"sensor_readings,device_id=thingy91x temperature=23.45,battery=98.76 {UNIX_NS}\n",
            f"sensor_readings,device_id=thingy91x temperature=-1.25,battery=98.0 {UNIX_NS + 15_000_000_000}\n",
        ]

    def test_reading_device_id_overrides_batch_device_id(self):
        payload = {
            "device_id": "batch-device",
            "readings": [{"device_id": "reading-device", "timestamp": UNIX_SECONDS, "battery": 50}],
        }
        assert ingest.build_lines(payload, TOPIC)[0].startswith("sensor_readings,device_id=reading-device ")

    @pytest.mark.parametrize("readings", [[], {}, "x"])
    def test_readings_must_be_non_empty_list(self, readings):
        with pytest.raises(ValueError, match="readings"):
            ingest.build_lines({"readings": readings}, TOPIC)

    def test_every_reading_must_be_an_object(self):
        with pytest.raises(ValueError, match="reading"):
            ingest.build_lines({"readings": [{"battery": 50}, 42]}, TOPIC)

    def test_one_invalid_reading_rejects_whole_batch(self):
        payload = {"readings": [{"timestamp": UNIX_SECONDS, "battery": 50}, {"timestamp": UNIX_SECONDS}]}
        with pytest.raises(ValueError):
            ingest.build_lines(payload, TOPIC)


class TestFirmwarePayloads:
    """Payloads in the exact form produced by app/src/data_transmission.c."""

    def test_firmware_batch_payload(self):
        raw = (
            '{"device_id":"thingy91x","readings":['
            '{"timestamp":1790143200,"temperature":23.45,"battery":98.76},'
            '{"timestamp":1790143215,"temperature":-1.25,"battery":98.00}]}'
        )
        lines = ingest.build_lines(json.loads(raw), TOPIC)
        assert lines == [
            f"sensor_readings,device_id=thingy91x temperature=23.45,battery=98.76 {UNIX_NS}\n",
            f"sensor_readings,device_id=thingy91x temperature=-1.25,battery=98.0 {UNIX_NS + 15_000_000_000}\n",
        ]

    def test_firmware_single_payload(self):
        raw = '{"device_id":"thingy91x","timestamp":1790143200,"temperature":23.45,"battery":98.76}'
        assert ingest.build_lines(json.loads(raw), TOPIC) == [
            f"sensor_readings,device_id=thingy91x temperature=23.45,battery=98.76 {UNIX_NS}\n",
        ]


class TestOnMessage:
    class Message:
        def __init__(self, payload, topic=TOPIC):
            self.payload = payload
            self.topic = topic

    def test_valid_message_is_written(self, monkeypatch):
        written = []
        monkeypatch.setattr(ingest, "write_questdb", written.append)
        raw = json.dumps({"timestamp": UNIX_SECONDS, "battery": 50}).encode()
        ingest.on_message(None, None, self.Message(raw))

        assert written == [[f"sensor_readings,device_id=thingy91x battery=50.0 {UNIX_NS}\n"]]

    @pytest.mark.parametrize("raw", [b"ikke json", b"[1, 2]", b"\xff", b'{"timestamp": 1}'])
    def test_invalid_message_is_rejected_without_crash(self, monkeypatch, raw):
        written = []
        monkeypatch.setattr(ingest, "write_questdb", written.append)
        ingest.on_message(None, None, self.Message(raw))
        assert written == []

    def test_questdb_error_does_not_crash(self, monkeypatch):
        def fail(lines):
            raise OSError("QuestDB nede")

        monkeypatch.setattr(ingest, "write_questdb", fail)
        raw = json.dumps({"timestamp": UNIX_SECONDS, "battery": 50}).encode()
        ingest.on_message(None, None, self.Message(raw))


class TestPersistentSession:
    def test_main_uses_fixed_id_and_persistent_connect(self, monkeypatch):
        client = Mock()
        factory = Mock(return_value=client)
        monkeypatch.setattr(ingest.mqtt, "Client", factory)
        ingest.main()
        factory.assert_called_once_with(client_id="projekt-c-questdb-ingest", protocol=ingest.mqtt.MQTTv5)
        client.username_pw_set.assert_called_once_with(ingest.MQTT_USER, ingest.MQTT_PASSWORD)
        client.reconnect_delay_set.assert_called_once_with(min_delay=2, max_delay=60)
        assert client.on_connect is ingest.on_connect
        assert client.on_subscribe is ingest.on_subscribe
        assert client.on_message is ingest.on_message
        args, kwargs = client.connect.call_args
        assert args == (ingest.MQTT_HOST, ingest.MQTT_PORT)
        assert kwargs["keepalive"] == 60
        assert kwargs["clean_start"] is False
        assert kwargs["properties"].packetType == PacketTypes.CONNECT
        assert kwargs["properties"].SessionExpiryInterval == 86400
        client.loop_forever.assert_called_once_with()

    @pytest.mark.parametrize("session_present", [False, True])
    def test_new_and_resumed_sessions_subscribe_with_qos_one(self, session_present):
        client = Mock()
        client.subscribe.return_value = (0, 1)
        ingest.on_connect(client, None, {"session present": session_present}, 0)
        client.subscribe.assert_called_once_with(ingest.MQTT_TOPIC, qos=1)

    def test_refused_connection_does_not_subscribe(self):
        client = Mock()
        ingest.on_connect(client, None, {}, 5)
        client.subscribe.assert_not_called()
