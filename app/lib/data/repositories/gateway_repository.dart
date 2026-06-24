import 'package:firebase_database/firebase_database.dart';
import '../../core/constants/firebase_paths.dart';
import '../../models/gateway.dart';

/// Repository for gateway data — reads state, writes mode changes.
class GatewayRepository {
  final DatabaseReference _db;

  GatewayRepository(this._db);

  /// Stream gateway data in real-time
  Stream<Gateway> streamGateway() {
    return _db.child(FirebasePaths.gateway()).onValue.map((event) {
      final data = event.snapshot.value as Map<dynamic, dynamic>?;
      if (data == null) {
        throw Exception('Gateway data is null');
      }
      return Gateway.fromJson(Map<String, dynamic>.from(data));
    });
  }

  /// Switch system mode (auto/manual)
  Future<void> setMode(String mode) async {
    await _db.child(FirebasePaths.gatewayMode()).set(mode);
  }
}
