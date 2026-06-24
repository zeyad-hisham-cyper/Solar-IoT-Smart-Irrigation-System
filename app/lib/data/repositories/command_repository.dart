import 'package:firebase_database/firebase_database.dart';
import '../../core/constants/firebase_paths.dart';
import '../../core/utils/timestamp_utils.dart';

/// Repository for writing commands to the control board via Firebase.
/// The control board polls commands/g1 every 2 seconds.
class CommandRepository {
  final DatabaseReference _db;

  CommandRepository(this._db);

  /// Open a zone valve with specified duration
  Future<void> openValve(String zoneId, {required int durationSeconds}) async {
    await _db.child(FirebasePaths.commands()).update({
      'z/$zoneId/v': true,
      'z/$zoneId/d': durationSeconds,
      'ts': TimestampUtils.nowEpoch(),
    });
  }

  /// Close a zone valve
  Future<void> closeValve(String zoneId) async {
    await _db.child(FirebasePaths.commands()).update({
      'z/$zoneId/v': false,
      'z/$zoneId/d': 0,
      'ts': TimestampUtils.nowEpoch(),
    });
  }

  /// Turn pump on
  Future<void> setPump(bool on) async {
    await _db.child(FirebasePaths.commands()).update({
      'pump': on,
      'ts': TimestampUtils.nowEpoch(),
    });
  }
}
