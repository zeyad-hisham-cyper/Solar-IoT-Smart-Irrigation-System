/// App-wide constants matching firmware timing and thresholds.
class AppConstants {
  // --- Staleness thresholds (match firmware timing) ---
  /// Zone data considered stale after this many seconds
  static const int zoneStaleWarningSeconds = 15;

  /// Zone considered offline after this many seconds
  static const int zoneOfflineSeconds = 60;

  /// Gateway considered offline after this many seconds
  static const int gatewayOfflineSeconds = 60;

  // --- Command safety ---
  /// Debounce delay after sending a command (matches firmware poll interval)
  static const int commandDebounceMsec = 2000;

  /// Debounce delay for config saves (threshold/duration)
  static const int configDebounceMsec = 500;

  // --- Validation ---
  /// Moisture threshold range
  static const int thresholdMin = 0;
  static const int thresholdMax = 100;

  /// Irrigation duration range (seconds)
  static const int durationMin = 1;
  static const int durationMax = 3600;

  // --- Firmware timing reference ---
  static const int firmwareUploadIntervalMs = 2000;
  static const int firmwarePollIntervalMs = 2000;
  static const int valveTimeoutMs = 600000; // 10 minutes
}
