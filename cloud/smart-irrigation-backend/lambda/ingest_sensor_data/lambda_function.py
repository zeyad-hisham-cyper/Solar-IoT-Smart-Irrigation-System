"""AWS Lambda entrypoint for ingesting sensor telemetry."""

from __future__ import annotations

import base64
import json
import traceback
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any, Dict, List

import psycopg2

from shared.db_utils import (
    AlertRecord,
    SensorReadingRecord,
    ZoneStatusUpdate,
    execute_many_sensor_readings,
    get_db_connection,
    has_unresolved_alert,
    insert_alerts,
    mark_stale_zones_offline,
    resolve_alerts_for_zone_types,
    update_zone_statuses,
)
from validators import ValidationError, validate_site_id, validate_zone_payload


@dataclass(slots=True)
class IngestSummary:
    site_id: int
    received_zones: int
    inserted_rows: int
    updated_zones: int
    inserted_alerts: int
    offline_alerts: int


HIGH_MOISTURE_THRESHOLD = 80.0
LOW_BATTERY_THRESHOLD = 20


def _log(level: str, message: str, **kwargs: Any) -> None:
    payload = {"level": level, "message": message, **kwargs}
    print(json.dumps(payload, default=str))


def lambda_handler(event: Dict[str, Any], context: Any) -> Dict[str, Any]:
    """Main Lambda handler."""
    _log("INFO", "IngestSensorData invoked", request_id=getattr(context, "aws_request_id", None))

    try:
        payload = parse_event_payload(event)
        summary = process_payload(payload)

        body = {
            "success": True,
            "site_id": summary.site_id,
            "received_zones": summary.received_zones,
            "inserted_rows": summary.inserted_rows,
            "updated_zones": summary.updated_zones,
            "inserted_alerts": summary.inserted_alerts,
            "offline_alerts": summary.offline_alerts,
        }

        _log("INFO", "IngestSensorData completed", **body)

        return {
            "statusCode": 200,
            "body": json.dumps(body),
        }

    except ValidationError as exc:
        _log("ERROR", "Validation failed", error=str(exc))
        return {
            "statusCode": 400,
            "body": json.dumps({"success": False, "error": str(exc)}),
        }
    except psycopg2.DatabaseError as exc:
        _log("ERROR", "Database error", error=str(exc))
        traceback.print_exc()
        return {
            "statusCode": 500,
            "body": json.dumps({"success": False, "error": "Database error"}),
        }
    except json.JSONDecodeError as exc:
        _log("ERROR", "JSON parse error", error=str(exc))
        return {
            "statusCode": 400,
            "body": json.dumps({"success": False, "error": "Invalid JSON payload"}),
        }
    except Exception as exc:
        _log("ERROR", "Unexpected error", error=str(exc))
        traceback.print_exc()
        return {
            "statusCode": 500,
            "body": json.dumps({"success": False, "error": "Internal server error"}),
        }


def parse_event_payload(event: Dict[str, Any]) -> Dict[str, Any]:
    """Parse payload from IoT or body-based event."""
    if isinstance(event, dict) and "site_id" in event and "zones" in event:
        return event

    if "body" in event:
        body = event["body"]
        if event.get("isBase64Encoded"):
            return json.loads(base64.b64decode(body).decode("utf-8"))
        if isinstance(body, str):
            return json.loads(body)
        if isinstance(body, dict):
            return body

    raise ValidationError("Unsupported event shape")


def process_payload(payload: Dict[str, Any]) -> IngestSummary:
    """Validate and persist telemetry payload."""
    site_id = validate_site_id(payload.get("site_id"))

    timestamp_raw = payload.get("timestamp")
    if timestamp_raw is None:
        reading_time = datetime.now(timezone.utc)
    else:
        try:
            reading_time = datetime.fromtimestamp(int(timestamp_raw), tz=timezone.utc)
        except (TypeError, ValueError, OSError) as exc:
            raise ValidationError("timestamp must be a valid Unix epoch integer") from exc

    zones = payload.get("zones")
    if not isinstance(zones, list) or not zones:
        raise ValidationError("zones must be a non-empty list")

    reading_rows: List[SensorReadingRecord] = []
    status_updates: List[ZoneStatusUpdate] = []
    alerts_to_insert: List[AlertRecord] = []

    for zone in zones:
        if not isinstance(zone, dict):
            raise ValidationError("each zone entry must be an object")

        normalized_zone, warnings = validate_zone_payload(zone)
        zone_id = normalized_zone["zone_id"]

        _log("INFO", "Zone payload validated", site_id=site_id, zone_id=zone_id, warnings=warnings)

        reading_rows.append(
            SensorReadingRecord(
                time=reading_time,
                site_id=site_id,
                zone_id=zone_id,
                temperature=normalized_zone["temperature"],
                moisture_percent=normalized_zone["moisture_percent"],
                moisture_state=normalized_zone["moisture_state"],
                flow_ml=normalized_zone["flow_ml"],
                valve_state=normalized_zone["valve_state"],
                status_flags=normalized_zone["status_flags"],
                rssi=normalized_zone["rssi"],
                battery_percent=normalized_zone["battery_percent"],
            )
        )

        status_updates.append(ZoneStatusUpdate(zone_id=zone_id, last_seen=reading_time))

        alerts_to_insert.extend(
            build_zone_alerts(
                site_id=site_id,
                zone_id=zone_id,
                moisture_percent=normalized_zone["moisture_percent"],
                battery_percent=normalized_zone["battery_percent"],
            )
        )

    with get_db_connection() as conn:
        inserted_rows = execute_many_sensor_readings(conn, reading_rows)
        updated_zones = update_zone_statuses(conn, status_updates)
        inserted_alerts = deduplicate_and_insert_alerts(conn, alerts_to_insert)
        offline_alerts = mark_stale_zones_offline(conn, stale_minutes=15)
        resolve_healthy_alerts(conn, reading_rows)

    return IngestSummary(
        site_id=site_id,
        received_zones=len(zones),
        inserted_rows=inserted_rows,
        updated_zones=updated_zones,
        inserted_alerts=inserted_alerts,
        offline_alerts=len(offline_alerts),
    )


def build_zone_alerts(
    site_id: int,
    zone_id: int,
    moisture_percent: float | None,
    battery_percent: int | None,
) -> List[AlertRecord]:
    """Build alert candidates from a reading."""
    alerts: List[AlertRecord] = []

    if moisture_percent is not None and moisture_percent > HIGH_MOISTURE_THRESHOLD:
        alerts.append(
            AlertRecord(
                site_id=site_id,
                zone_id=zone_id,
                severity="warning",
                alert_type="high_moisture",
                message=f"Zone {zone_id} moisture is {moisture_percent:.1f}%, possible leak or over-irrigation",
            )
        )

    if battery_percent is not None and battery_percent < LOW_BATTERY_THRESHOLD:
        alerts.append(
            AlertRecord(
                site_id=site_id,
                zone_id=zone_id,
                severity="warning",
                alert_type="low_battery",
                message=f"Zone {zone_id} battery is low at {battery_percent}%",
            )
        )

    return alerts


def deduplicate_and_insert_alerts(conn: Any, alerts: List[AlertRecord]) -> int:
    """Insert alerts only if unresolved duplicate does not already exist."""
    filtered_alerts: List[AlertRecord] = []

    for alert in alerts:
        if alert.zone_id is None:
            filtered_alerts.append(alert)
            continue
        if not has_unresolved_alert(conn, alert.zone_id, alert.alert_type):
            filtered_alerts.append(alert)

    return insert_alerts(conn, filtered_alerts)


def resolve_healthy_alerts(conn: Any, reading_rows: List[SensorReadingRecord]) -> None:
    """Resolve prior alerts if reading is healthy now."""
    for row in reading_rows:
        alert_types_to_resolve: List[str] = []

        if row.moisture_percent is not None and row.moisture_percent <= HIGH_MOISTURE_THRESHOLD:
            alert_types_to_resolve.append("high_moisture")

        if row.battery_percent is not None and row.battery_percent >= LOW_BATTERY_THRESHOLD:
            alert_types_to_resolve.append("low_battery")

        alert_types_to_resolve.append("sensor_offline")

        resolve_alerts_for_zone_types(
            conn,
            zone_id=row.zone_id,
            alert_types=alert_types_to_resolve,
        )