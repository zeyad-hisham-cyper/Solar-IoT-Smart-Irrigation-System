import 'package:firebase_database/firebase_database.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../data/repositories/command_repository.dart';

/// Provider for the command repository
final commandRepositoryProvider = Provider<CommandRepository>((ref) {
  return CommandRepository(FirebaseDatabase.instance.ref());
});
