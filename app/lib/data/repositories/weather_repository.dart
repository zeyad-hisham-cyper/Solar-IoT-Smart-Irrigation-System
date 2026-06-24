import 'package:firebase_database/firebase_database.dart';
import '../../core/constants/firebase_paths.dart';
import '../../models/weather.dart';

/// Repository for weather data — read-only stream from Firebase.
/// The control board fetches weather from OpenWeather API and writes to /weather.
class WeatherRepository {
  final DatabaseReference _db;

  WeatherRepository(this._db);

  /// Stream weather data in real-time
  Stream<Weather> streamWeather() {
    return _db.child(FirebasePaths.weather()).onValue.map((event) {
      final data = event.snapshot.value as Map<dynamic, dynamic>?;
      if (data == null) {
        return const Weather(rain: false, temp: 0, ts: 0);
      }
      return Weather.fromJson(Map<String, dynamic>.from(data));
    });
  }
}
