import 'package:flutter/foundation.dart';
import '../constants/app_constants.dart';

/// Helpers for converting between Unix epoch timestamps and DateTime.
/// Handles both seconds and milliseconds epoch formats automatically,
/// with robust null handling and debug logging.
class TimestampUtils {
  /// Normalize any epoch value (seconds or milliseconds) to exactly milliseconds.
  /// A valid current Unix timestamp in seconds is ~1.7 billion (10 digits).
  /// In milliseconds, it's ~1.7 trillion (13 digits).
  static int normalizeToMillis(int epoch) {
    if (epoch == 0) return 0;
    if (epoch < 0) return epoch; // Negative values mean time not synced
    // If less than 100 billion, it's definitely in seconds.
    if (epoch < 100000000000) {
      return epoch * 1000;
    }
    // Otherwise it's already in milliseconds.
    return epoch;
  }

  /// Get current epoch seconds
  static int nowEpoch() {
    return DateTime.now().millisecondsSinceEpoch ~/ 1000;
  }

  /// Get the age in seconds of a timestamp, handling both formats safely.
  static int ageInSeconds(int? epochRaw, {String debugLabel = 'Unknown'}) {
    if (epochRaw == null || epochRaw == 0 || epochRaw == 4294967295) {
      debugPrint('[$debugLabel] Invalid/null/max epoch: $epochRaw -> age is infinite');
      return 999999;
    }
    if (epochRaw < 0) {
      debugPrint('[$debugLabel] Time not synced yet ($epochRaw).');
      return -1;
    }

    final epochMillis = normalizeToMillis(epochRaw);
    final nowMillis = DateTime.now().millisecondsSinceEpoch;
    
    // Calculate difference in milliseconds, then convert to seconds
    final diffMillis = nowMillis - epochMillis;
    final diffSeconds = diffMillis ~/ 1000;

    // Reject future timestamps
    if (diffSeconds < -60) {
      debugPrint('[$debugLabel] Future timestamp detected. Diff: ${diffSeconds}s -> age is infinite');
      return 999999;
    }

    debugPrint('[$debugLabel] Raw: $epochRaw | Normalized Millis: $epochMillis | Now Millis: $nowMillis | Diff Seconds: $diffSeconds');

    return diffSeconds < 0 ? 0 : diffSeconds;
  }

  /// Get human-readable "time ago" string
  static String timeAgo(int? epochRaw) {
    if (epochRaw == null || epochRaw == 0 || epochRaw == 4294967295) return 'Unavailable';
    if (epochRaw < 0) return 'Syncing time...';

    final diff = ageInSeconds(epochRaw, debugLabel: 'timeAgo');
    
    if (diff < 0) return 'Syncing time...';
    if (diff == 999999) return 'Unavailable';
    if (diff < 5) return 'Just now';
    if (diff < 60) return '${diff}s ago';
    if (diff < 3600) return '${diff ~/ 60}m ago';
    if (diff < 86400) return '${diff ~/ 3600}h ago';
    return '${diff ~/ 86400}d ago';
  }

  /// Check if a timestamp is stale (older than threshold seconds)
  static bool isStale(int? epochRaw, int thresholdSeconds, {String debugLabel = 'Unknown'}) {
    final age = ageInSeconds(epochRaw, debugLabel: debugLabel);
    if (age < 0) return false; // Not stale if time isn't synced yet
    final stale = age > thresholdSeconds;
    debugPrint('[$debugLabel] Age: ${age}s | Threshold: ${thresholdSeconds}s | isStale: $stale');
    return stale;
  }

  /// Check if a zone is in warning-stale state
  static bool isZoneStale(int? lastUpdate, String zoneId) {
    return isStale(lastUpdate, AppConstants.zoneStaleWarningSeconds, debugLabel: 'Zone $zoneId Stale Check');
  }

  /// Check if a zone is offline
  static bool isZoneOffline(int? lastUpdate, String zoneId) {
    return isStale(lastUpdate, AppConstants.zoneOfflineSeconds, debugLabel: 'Zone $zoneId Offline Check');
  }

  /// Check if the gateway is offline
  static bool isGatewayOffline(int? lastSeen) {
    return isStale(lastSeen, AppConstants.gatewayOfflineSeconds, debugLabel: 'Gateway Offline Check');
  }
}
