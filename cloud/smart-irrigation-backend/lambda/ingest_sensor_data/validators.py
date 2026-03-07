"""Validation helpers for sensor telemetry ingestion."""

from __future__ import annotations

from typing import Any, Dict, List, Tuple


VALID_MOISTURE_STATES = {"DRY", "NORMAL", "WET"}
VALID_VALVE_STATES = {"OPEN", "CLOSED", "ERROR"}


class ValidationError(Exception):
    """Raised when telemetry payload validation fails."""


def validate_site_id(value: Any) -> int:
    """Validate numeric site_id."""
    if value is None:
        raise ValidationError("site_id is required")

    try:
        site_id = int(value)
    except (TypeError, ValueError) as exc:
        raise ValidationError("site_id must be an integer") from exc

    if site_id <= 0:
        raise ValidationError("site_id must be greater than 0")
    return site_id


def validate_zone_payload(zone: Dict[str, Any]) -> Tuple[Dict[str, Any], List[str]]:
    """Validate a single zone telemetry object."""
    warnings: List[str] = []

    if "zone_id" not in zone:
        raise ValidationError("zone_id is required for every zone")

    try:
        zone_id = int(zone["zone_id"])
    except (TypeError, ValueError) as exc:
        raise ValidationError("zone_id must be an integer") from exc

    if zone_id <= 0:
        raise ValidationError("zone_id must be greater than 0")

    temperature = _optional_float(zone.get("temperature"), "temperature", -20.0, 85.0)
    moisture_percent = _optional_float(
        zone.get("moisture_percent"), "moisture_percent", 0.0, 100.0
    )

    moisture_state = zone.get("moisture_state")
    if moisture_state is not None:
        moisture_state = str(moisture_state).upper()
        if moisture_state not in VALID_MOISTURE_STATES:
            raise ValidationError(
                f"Invalid moisture_state '{moisture_state}', expected one of {sorted(VALID_MOISTURE_STATES)}"
            )

    flow_ml = _optional_float(zone.get("flow_ml", 0), "flow_ml", 0.0, 1_000_000.0)
    flow_ml = 0.0 if flow_ml is None else flow_ml

    valve_state = zone.get("valve_state")
    if valve_state is not None:
        valve_state = str(valve_state).upper()
        if valve_state not in VALID_VALVE_STATES:
            raise ValidationError(
                f"Invalid valve_state '{valve_state}', expected one of {sorted(VALID_VALVE_STATES)}"
            )

    status_flags = _optional_int(zone.get("status_flags", 0), "status_flags", 0, 2**31 - 1)
    status_flags = 0 if status_flags is None else status_flags

    rssi = _optional_int(zone.get("rssi"), "rssi", -150, 0)
    battery_percent = _optional_int(zone.get("battery_percent"), "battery_percent", 0, 100)

    if battery_percent is not None and battery_percent < 20:
        warnings.append(f"zone {zone_id}: low battery detected ({battery_percent}%)")

    if moisture_percent is not None and moisture_percent > 80:
        warnings.append(f"zone {zone_id}: high moisture detected ({moisture_percent}%)")

    return (
        {
            "zone_id": zone_id,
            "temperature": temperature,
            "moisture_percent": moisture_percent,
            "moisture_state": moisture_state,
            "flow_ml": flow_ml,
            "valve_state": valve_state,
            "status_flags": status_flags,
            "rssi": rssi,
            "battery_percent": battery_percent,
        },
        warnings,
    )


def _optional_float(value: Any, name: str, min_value: float, max_value: float) -> float | None:
    """Validate optional float within range."""
    if value is None:
        return None
    try:
        parsed = float(value)
    except (TypeError, ValueError) as exc:
        raise ValidationError(f"{name} must be numeric") from exc
    if parsed < min_value or parsed > max_value:
        raise ValidationError(f"{name} out of range [{min_value}, {max_value}]")
    return parsed


def _optional_int(value: Any, name: str, min_value: int, max_value: int) -> int | None:
    """Validate optional integer within range."""
    if value is None:
        return None
    try:
        parsed = int(value)
    except (TypeError, ValueError) as exc:
        raise ValidationError(f"{name} must be an integer") from exc
    if parsed < min_value or parsed > max_value:
        raise ValidationError(f"{name} out of range [{min_value}, {max_value}]")
    return parsed