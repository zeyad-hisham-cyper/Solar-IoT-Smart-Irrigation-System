/// Weather model — cached weather data from OpenWeather via the control board.
/// Firebase path: weather/
class Weather {
  final bool rain;     // true = currently raining (field name: "rain")
  final num temp;      // Temperature °C
  final int ts;        // Fetch timestamp (Unix epoch)

  const Weather({
    required this.rain,
    required this.temp,
    required this.ts,
  });

  factory Weather.fromJson(Map<String, dynamic> json) {
    return Weather(
      rain: (json['rain'] as bool?) ?? false,
      temp: (json['temp'] as num?) ?? 0,
      ts: (json['ts'] as num?)?.toInt() ?? 0,
    );
  }

  Map<String, dynamic> toJson() {
    return {
      'rain': rain,
      'temp': temp,
      'ts': ts,
    };
  }
}
