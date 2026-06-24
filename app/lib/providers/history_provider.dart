import 'package:firebase_database/firebase_database.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../data/repositories/history_repository.dart';
import '../models/zone_reading.dart';

/// Repository provider (single instance keeps the in-memory de-dupe guard).
final historyRepositoryProvider = Provider<HistoryRepository>((ref) {
  return HistoryRepository(FirebaseDatabase.instance.ref());
});

/// Stream of the latest history readings for a zone (oldest→newest).
final zoneHistoryProvider =
    StreamProvider.family<List<ZoneReading>, String>((ref, zoneId) {
  final repo = ref.watch(historyRepositoryProvider);
  return repo.streamHistory(zoneId, limit: 50);
});
