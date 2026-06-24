import 'package:firebase_database/firebase_database.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../data/repositories/gateway_repository.dart';
import '../models/gateway.dart';

/// Repository provider
final gatewayRepositoryProvider = Provider<GatewayRepository>((ref) {
  return GatewayRepository(FirebaseDatabase.instance.ref());
});

/// Stream provider for real-time gateway data
final gatewayStreamProvider = StreamProvider<Gateway>((ref) {
  final repo = ref.watch(gatewayRepositoryProvider);
  return repo.streamGateway();
});

/// Derived provider: is gateway online?
final gatewayOnlineProvider = Provider<bool>((ref) {
  final gatewayAsync = ref.watch(gatewayStreamProvider);
  return gatewayAsync.when(
    data: (gateway) => gateway.isConnected,
    loading: () => false,
    error: (_, _) => false,
  );
});

/// Derived provider: current system mode
final systemModeProvider = Provider<String>((ref) {
  final gatewayAsync = ref.watch(gatewayStreamProvider);
  return gatewayAsync.when(
    data: (gateway) => gateway.mode,
    loading: () => 'auto',
    error: (_, _) => 'auto',
  );
});

/// Derived provider: is manual mode?
final isManualModeProvider = Provider<bool>((ref) {
  return ref.watch(systemModeProvider) == 'manual';
});
