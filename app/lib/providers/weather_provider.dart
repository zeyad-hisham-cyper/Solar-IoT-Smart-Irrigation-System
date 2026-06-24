import 'package:firebase_database/firebase_database.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../data/repositories/weather_repository.dart';
import '../models/weather.dart';

/// Repository provider
final weatherRepositoryProvider = Provider<WeatherRepository>((ref) {
  return WeatherRepository(FirebaseDatabase.instance.ref());
});

/// Stream provider for real-time weather data
final weatherStreamProvider = StreamProvider<Weather>((ref) {
  final repo = ref.watch(weatherRepositoryProvider);
  return repo.streamWeather();
});
