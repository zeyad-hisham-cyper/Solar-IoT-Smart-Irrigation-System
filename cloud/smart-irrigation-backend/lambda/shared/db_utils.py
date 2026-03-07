"""Shared database utilities for Smart Irrigation Lambda functions."""

from __future__ import annotations

import json
import os
from contextlib import contextmanager
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from typing import Any, Dict, Generator, Iterable, List, Optional, Sequence

import boto3
import psycopg2
import psycopg2.extras
from psycopg2.extensions import connection as PGConnection
from psycopg2.extensions import cursor as PGCursor


AWS_REGION = os.environ.get("AWS_REGION", "il-central-1")
DB_HOST = os.environ["DB_HOST"]
DB_PORT = int(os.environ.get("DB_PORT", "5432"))
DB_NAME = os.environ["DB_NAME"]
DB_USER = os.environ["DB_USER"]
DB_PASSWORD_SECRET_NAME = os.environ.get("DB_PASSWORD_SECRET_NAME", "")
DB_PASSWORD_ENV = os.environ.get("DB_PASSWORD")

secrets_client = boto3.client("secretsmanager", region_name=AWS_REGION)


@dataclass(slots=True)
class SensorReadingRecord:
    time: datetime
    site_id: int
    zone_id: int
    temperature: Optional[float]
    moisture_percent: Optional[float]
    moisture_state: Optional[str]
    flow_ml: float
    valve_state: Optional[str]
    status_flags: int
    rssi: Optional[int]
    battery_percent: Optional[int]


@dataclass(slots=True)
class ZoneStatusUpdate:
    zone_id: int
    last_seen: datetime
    is_online: bool = True


@dataclass(slots=True)
class AlertRecord:
    site_id: int
    zone_id: Optional[int]
    severity: str
    alert_type: str
    message: str


def get_secret(secret_name: str) -> Dict[str, Any]:
    """Retrieve and parse a JSON secret from AWS Secrets Manager."""
    response = secrets_client.get_secret_value(SecretId=secret_name)
    secret_string = response.get("SecretString")
    if not secret_string:
        raise ValueError(f"Secret '{secret_name}' does not contain SecretString")
    return json.loads(secret_string)


def get_db_password() -> str:
    """Resolve DB password from Secrets Manager or env variable."""
    if DB_PASSWORD_SECRET_NAME:
        secret = get_secret(DB_PASSWORD_SECRET_NAME)
        password = secret.get("password")
        if not password:
            raise ValueError(
                f"Secret '{DB_PASSWORD_SECRET_NAME}' does not contain key 'password'"
            )
        return str(password)

    if DB_PASSWORD_ENV:
        return DB_PASSWORD_ENV

    raise ValueError("Set DB_PASSWORD_SECRET_NAME or DB_PASSWORD.")


@contextmanager
def get_db_connection() -> Generator[PGConnection, None, None]:
    """Context manager for PostgreSQL connections."""
    conn = psycopg2.connect(
        host=DB_HOST,
        port=DB_PORT,
        dbname=DB_NAME,
        user=DB_USER,
        password=get_db_password(),
        connect_timeout=10,
        cursor_factory=psycopg2.extras.RealDictCursor,
    )
    try:
        yield conn
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


@contextmanager
def get_cursor(conn: PGConnection) -> Generator[PGCursor, None, None]:
    """Context manager for cursor lifecycle."""
    cursor = conn.cursor()
    try:
        yield cursor
    finally:
        cursor.close()


def execute_many_sensor_readings(conn: PGConnection, rows: Sequence[SensorReadingRecord]) -> int:
    """Insert multiple sensor readings efficiently."""
    if not rows:
        return 0

    sql = """
        INSERT INTO sensor_readings (
            time,
            site_id,
            zone_id,
            temperature,
            moisture_percent,
            moisture_state,
            flow_ml,
            valve_state,
            status_flags,
            rssi,
            battery_percent
        ) VALUES %s
    """

    values = [
        (
            row.time,
            row.site_id,
            row.zone_id,
            row.temperature,
            row.moisture_percent,
            row.moisture_state,
            row.flow_ml,
            row.valve_state,
            row.status_flags,
            row.rssi,
            row.battery_percent,
        )
        for row in rows
    ]

    with get_cursor(conn) as cur:
        psycopg2.extras.execute_values(cur, sql, values, page_size=100)

    return len(rows)


def update_zone_statuses(conn: PGConnection, updates: Sequence[ZoneStatusUpdate]) -> int:
    """Update online status and last_seen for multiple zones."""
    if not updates:
        return 0

    sql = """
        UPDATE zones AS z
        SET
            is_online = v.is_online,
            last_seen = v.last_seen,
            updated_at = NOW()
        FROM (VALUES %s) AS v(zone_id, is_online, last_seen)
        WHERE z.zone_id = v.zone_id
    """

    values = [(u.zone_id, u.is_online, u.last_seen) for u in updates]

    with get_cursor(conn) as cur:
        psycopg2.extras.execute_values(cur, sql, values, page_size=100)

    return len(updates)


def insert_alerts(conn: PGConnection, alerts: Sequence[AlertRecord]) -> int:
    """Insert alert rows."""
    if not alerts:
        return 0

    sql = """
        INSERT INTO alerts (
            site_id,
            zone_id,
            severity,
            alert_type,
            message,
            resolved,
            created_at
        ) VALUES %s
    """

    values = [
        (
            alert.site_id,
            alert.zone_id,
            alert.severity,
            alert.alert_type,
            alert.message,
            False,
            datetime.now(timezone.utc),
        )
        for alert in alerts
    ]

    with get_cursor(conn) as cur:
        psycopg2.extras.execute_values(cur, sql, values, page_size=100)

    return len(alerts)


def has_unresolved_alert(conn: PGConnection, zone_id: int, alert_type: str) -> bool:
    """Check whether an unresolved alert already exists."""
    with get_cursor(conn) as cur:
        cur.execute(
            """
            SELECT 1
            FROM alerts
            WHERE zone_id = %s
              AND alert_type = %s
              AND resolved = FALSE
            LIMIT 1
            """,
            (zone_id, alert_type),
        )
        return cur.fetchone() is not None


def resolve_alerts_for_zone_types(
    conn: PGConnection,
    zone_id: int,
    alert_types: Iterable[str],
) -> int:
    """Resolve unresolved alerts for a zone and given alert types."""
    alert_types_list = list(alert_types)
    if not alert_types_list:
        return 0

    with get_cursor(conn) as cur:
        cur.execute(
            """
            UPDATE alerts
            SET resolved = TRUE,
                resolved_at = NOW()
            WHERE zone_id = %s
              AND resolved = FALSE
              AND alert_type = ANY(%s)
            """,
            (zone_id, alert_types_list),
        )
        return cur.rowcount


def mark_stale_zones_offline(conn: PGConnection, stale_minutes: int = 15) -> List[AlertRecord]:
    """Mark stale zones offline and generate offline alerts."""
    threshold = datetime.now(timezone.utc) - timedelta(minutes=stale_minutes)

    with get_cursor(conn) as cur:
        cur.execute(
            """
            SELECT z.zone_id, z.site_id, z.last_seen
            FROM zones z
            WHERE z.active = TRUE
              AND z.last_seen IS NOT NULL
              AND z.last_seen < %s
              AND z.is_online = TRUE
            """,
            (threshold,),
        )
        stale_zones = cur.fetchall()

        cur.execute(
            """
            UPDATE zones
            SET is_online = FALSE,
                updated_at = NOW()
            WHERE active = TRUE
              AND last_seen IS NOT NULL
              AND last_seen < %s
              AND is_online = TRUE
            """,
            (threshold,),
        )

    alerts_to_insert: List[AlertRecord] = []
    for zone in stale_zones:
        if not has_unresolved_alert(conn, zone["zone_id"], "sensor_offline"):
            alerts_to_insert.append(
                AlertRecord(
                    site_id=zone["site_id"],
                    zone_id=zone["zone_id"],
                    severity="warning",
                    alert_type="sensor_offline",
                    message=f"Zone {zone['zone_id']} has not reported since {zone['last_seen']}",
                )
            )

    if alerts_to_insert:
        insert_alerts(conn, alerts_to_insert)

    return alerts_to_insert