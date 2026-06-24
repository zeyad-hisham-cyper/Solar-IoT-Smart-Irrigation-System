import 'package:flutter/foundation.dart';
import '../core/utils/timestamp_utils.dart';

/// Gateway model — represents the control board state in Firebase.
/// Firebase path: gateways/g1
class Gateway {
  final String mode;       // "auto" or "manual"
  final bool pump;         // true = pump is running
  final int waterLevel;    // 0-100 (reserved for future sensor)
  final int lastSeen;      // Unix epoch (seconds or milliseconds)

  const Gateway({
    required this.mode,
    required this.pump,
    required this.waterLevel,
    required this.lastSeen,
  });

  /// Gateway is connected if lastSeen is within the threshold.
  bool get isConnected {
    final online = !TimestampUtils.isGatewayOffline(lastSeen);
    debugPrint('[Gateway State] isConnected evaluated to: $online');
    return online;
  }

  factory Gateway.fromJson(Map<String, dynamic> json) {
    final rawLastSeen = (json['lastSeen'] as num?)?.toInt() ?? 0;
    debugPrint('[Gateway Parse] Raw payload: $json');
    debugPrint('[Gateway Parse] Parsed raw lastSeen: $rawLastSeen');

    return Gateway(
      mode: (json['mode'] as String?) ?? 'auto',
      pump: (json['pump'] as bool?) ?? false,
      waterLevel: (json['waterLevel'] as num?)?.toInt() ?? 0,
      lastSeen: rawLastSeen,
    );
  }

  Map<String, dynamic> toJson() {
    return {
      'mode': mode,
      'pump': pump,
      'waterLevel': waterLevel,
      'lastSeen': lastSeen,
    };
  }

  Gateway copyWith({
    String? mode,
    bool? pump,
    int? waterLevel,
    int? lastSeen,
  }) {
    return Gateway(
      mode: mode ?? this.mode,
      pump: pump ?? this.pump,
      waterLevel: waterLevel ?? this.waterLevel,
      lastSeen: lastSeen ?? this.lastSeen,
    );
  }
}
