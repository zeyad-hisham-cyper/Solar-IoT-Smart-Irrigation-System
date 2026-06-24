import 'package:firebase_database/firebase_database.dart';
import '../../core/constants/firebase_paths.dart';
import '../../models/zone_data.dart';

/// Repository for zone data — reads sensor telemetry, writes config changes.
class ZoneRepository {
  final DatabaseReference _db;

  ZoneRepository(this._db);

  /// Stream zone data in real-time
  Stream<ZoneData> streamZone(String zoneId) {
    return _db.child(FirebasePaths.zone(zoneId)).onValue.map((event) {
      final data = event.snapshot.value as Map<dynamic, dynamic>?;
      if (data == null) {
        throw Exception('Zone $zoneId data is null');
      }
      return ZoneData.fromJson(Map<String, dynamic>.from(data), zoneId: zoneId);
    });
  }

  /// Update moisture threshold for a zone
  Future<void> setThreshold(String zoneId, int threshold) async {
    await _db.child(FirebasePaths.zoneThreshold(zoneId)).set(threshold);
  }

  /// Update irrigation duration for a zone
  Future<void> setDuration(String zoneId, int durationSeconds) async {
    await _db.child(FirebasePaths.zoneDuration(zoneId)).set(durationSeconds);
  }
}
