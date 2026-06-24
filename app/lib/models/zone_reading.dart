import '../core/utils/timestamp_utils.dart';

/// A single historical reading for a zone.
///
/// Firebase path: zones/{zoneId}/history/{readingId}
/// Stored beside the existing latest-value fields — never replaces them.
class ZoneReading {
  final num temperature;
  final int moisture;
  final num waterConsumption;

  /// Server timestamp in epoch milliseconds.
  final int timestamp;

  const ZoneReading({
    required this.temperature,
    required this.moisture,
    required this.waterConsumption,
    required this.timestamp,
  });

  DateTime get dateTime => DateTime.fromMillisecondsSinceEpoch(
        TimestampUtils.normalizeToMillis(timestamp),
      );

  factory ZoneReading.fromJson(Map<String, dynamic> json) {
    return ZoneReading(
      temperature: (json['temperature'] as num?) ?? 0,
      moisture: (json['moisture'] as num?)?.toInt() ?? 0,
      waterConsumption: (json['waterConsumption'] as num?) ?? 0,
      timestamp: (json['timestamp'] as num?)?.toInt() ?? 0,
    );
  }
}
