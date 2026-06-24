import 'package:flutter/foundation.dart';
import '../core/utils/timestamp_utils.dart';

/// Zone sensor data model — represents a single zone's telemetry + config.
/// Firebase path: zones/z1, zones/z2
class ZoneData {
  final String g;      // Gateway ID (e.g., "g1")
  final int sm;        // Soil moisture 0-100%
  final num t;         // Temperature °C
  final num f;         // Flow rate / total volume
  final bool v;        // Valve state: true = open
  final int lu;        // Last update (Unix epoch — seconds or milliseconds)
  final int th;        // Moisture threshold %
  final int dur;       // Irrigation duration (seconds)

  const ZoneData({
    required this.g,
    required this.sm,
    required this.t,
    required this.f,
    required this.v,
    required this.lu,
    required this.th,
    required this.dur,
  });

  /// Zone is connected if lu is within the valid threshold.
  bool get isConnected {
    final online = !TimestampUtils.isZoneOffline(lu, g);
    debugPrint('[Zone $g State] isConnected evaluated to: $online');
    return online;
  }

  factory ZoneData.fromJson(Map<String, dynamic> json, {String zoneId = 'Unknown'}) {
    final rawLu = (json['lu'] as num?)?.toInt() ?? 0;
    debugPrint('[Zone $zoneId Parse] Raw payload: $json');
    debugPrint('[Zone $zoneId Parse] Parsed raw lu: $rawLu');

    return ZoneData(
      g: (json['g'] as String?) ?? 'g1',
      sm: (json['sm'] as num?)?.toInt() ?? 0,
      t: (json['t'] as num?) ?? 0,
      f: (json['f'] as num?) ?? 0,
      v: (json['v'] as bool?) ?? false,
      lu: rawLu,
      th: (json['th'] as num?)?.toInt() ?? 35,
      dur: (json['dur'] as num?)?.toInt() ?? 120,
    );
  }

  Map<String, dynamic> toJson() {
    return {
      'g': g,
      'sm': sm,
      't': t,
      'f': f,
      'v': v,
      'lu': lu,
      'th': th,
      'dur': dur,
    };
  }

  ZoneData copyWith({
    String? g,
    int? sm,
    num? t,
    num? f,
    bool? v,
    int? lu,
    int? th,
    int? dur,
  }) {
    return ZoneData(
      g: g ?? this.g,
      sm: sm ?? this.sm,
      t: t ?? this.t,
      f: f ?? this.f,
      v: v ?? this.v,
      lu: lu ?? this.lu,
      th: th ?? this.th,
      dur: dur ?? this.dur,
    );
  }
}
