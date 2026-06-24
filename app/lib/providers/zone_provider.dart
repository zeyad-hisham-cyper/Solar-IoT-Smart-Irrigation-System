import 'package:firebase_database/firebase_database.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../data/repositories/zone_repository.dart';
import '../models/zone_data.dart';

/// Repository provider
final zoneRepositoryProvider = Provider<ZoneRepository>((ref) {
  return ZoneRepository(FirebaseDatabase.instance.ref());
});

/// Stream provider for real-time zone data (parameterized by zone ID)
final zoneStreamProvider =
    StreamProvider.family<ZoneData, String>((ref, zoneId) {
  final repo = ref.watch(zoneRepositoryProvider);
  return repo.streamZone(zoneId);
});
