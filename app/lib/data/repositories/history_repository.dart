import 'package:firebase_database/firebase_database.dart';
import '../../core/constants/firebase_paths.dart';
import '../../models/zone_reading.dart';

/// Repository for per-zone historical readings.
///
/// History is stored as an additive subtree at `zones/{zoneId}/history` so the
/// existing latest-value fields (sm, t, f, v, lu, th, dur) are never touched.
/// Readings are appended with `push()` keys and a server timestamp.
class HistoryRepository {
  final DatabaseReference _db;

  HistoryRepository(this._db);

  /// In-memory de-dupe guard: last source `lu` we persisted per zone, and the
  /// wall-clock time we last wrote. Prevents duplicate/flooding writes.
  final Map<String, int> _lastSourceLu = {};
  final Map<String, DateTime> _lastWriteAt = {};

  /// Minimum spacing between persisted readings for a single zone.
  static const Duration minInterval = Duration(seconds: 60);

  /// Stream the most recent [limit] readings, ordered oldest→newest so charts
  /// can plot left→right. Returns an empty list when no history exists yet.
  Stream<List<ZoneReading>> streamHistory(String zoneId, {int limit = 50}) {
    final query = _db
        .child(FirebasePaths.zoneHistory(zoneId))
        .orderByChild('timestamp')
        .limitToLast(limit);

    return query.onValue.map((event) {
      final raw = event.snapshot.value;
      if (raw == null) return <ZoneReading>[];

      final readings = <ZoneReading>[];
      if (raw is Map) {
        raw.forEach((key, value) {
          if (value is Map) {
            readings.add(
              ZoneReading.fromJson(Map<String, dynamic>.from(value)),
            );
          }
        });
      }
      readings.sort((a, b) => a.timestamp.compareTo(b.timestamp));
      return readings;
    });
  }

  /// Append a reading for [zoneId] if enough has changed since the last write.
  ///
  /// Guards against duplicates: skips when the source [sourceLu] (the zone's
  /// last-update epoch) is unchanged, or when [minInterval] hasn't elapsed.
  /// Uses [ServerValue.timestamp] so ordering is server-authoritative.
  Future<void> recordReadingIfNeeded(
    String zoneId, {
    required num temperature,
    required int moisture,
    required num waterConsumption,
    required int sourceLu,
  }) async {
    // Skip invalid / unsynced source timestamps.
    if (sourceLu <= 0) return;

    final now = DateTime.now();
    final lastLu = _lastSourceLu[zoneId];
    final lastAt = _lastWriteAt[zoneId];

    final sameReading = lastLu != null && lastLu == sourceLu;
    final tooSoon = lastAt != null && now.difference(lastAt) < minInterval;
    if (sameReading || tooSoon) return;

    // Optimistically mark so concurrent calls don't double-write.
    _lastSourceLu[zoneId] = sourceLu;
    _lastWriteAt[zoneId] = now;

    try {
      await _db.child(FirebasePaths.zoneHistory(zoneId)).push().set({
        'temperature': temperature,
        'moisture': moisture,
        'waterConsumption': waterConsumption,
        'timestamp': ServerValue.timestamp,
      });
    } catch (_) {
      // Roll back the guard so a later attempt can retry.
      _lastSourceLu.remove(zoneId);
      _lastWriteAt.remove(zoneId);
      rethrow;
    }
  }
}
