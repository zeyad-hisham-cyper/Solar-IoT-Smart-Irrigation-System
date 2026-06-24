/// System operation mode
enum SystemMode {
  auto,
  manual;

  static SystemMode fromString(String value) {
    return value == 'manual' ? SystemMode.manual : SystemMode.auto;
  }

  String toFirebaseValue() {
    return this == SystemMode.manual ? 'manual' : 'auto';
  }
}

/// Valve state
enum ValveState {
  open,
  closed;

  static ValveState fromBool(bool isOpen) {
    return isOpen ? ValveState.open : ValveState.closed;
  }

  bool toBool() => this == ValveState.open;
}

/// Zone connectivity status based on last-update freshness
enum ZoneStatus {
  online,
  stale,
  offline;
}

/// Pump state
enum PumpState {
  on,
  off;

  static PumpState fromBool(bool isOn) {
    return isOn ? PumpState.on : PumpState.off;
  }

  bool toBool() => this == PumpState.on;
}
