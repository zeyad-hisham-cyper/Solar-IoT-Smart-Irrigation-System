import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/constants/firebase_paths.dart';
import '../models/gateway.dart';
import '../models/zone_data.dart';
import 'gateway_provider.dart';
import 'zone_provider.dart';

enum AlertType { info, warning, error }

class SystemAlert {
  final String message;
  final DateTime timestamp;
  final AlertType type;

  SystemAlert(this.message, this.timestamp, this.type);
}

class AlertsNotifier extends Notifier<List<SystemAlert>> {
  Gateway? _lastGateway;
  final Map<String, ZoneData> _lastZones = {};

  @override
  List<SystemAlert> build() {
    // Listen to gateway
    ref.listen<AsyncValue<Gateway>>(gatewayStreamProvider, (previous, next) {
      if (next.hasValue && next.value != null) {
        final gw = next.value!;
        if (_lastGateway != null) {
          if (gw.isConnected != _lastGateway!.isConnected) {
            if (gw.isConnected) {
              _addAlert('Gateway came online', AlertType.info);
            } else {
              _addAlert('Gateway went offline', AlertType.error);
            }
          }
          if (gw.mode != _lastGateway!.mode) {
            _addAlert('System mode changed to ${gw.mode.toUpperCase()}', AlertType.info);
          }
          if (gw.pump != _lastGateway!.pump) {
            _addAlert('Pump turned ${gw.pump ? 'ON' : 'OFF'}', AlertType.info);
          }
          if (gw.waterLevel < 20 && _lastGateway!.waterLevel >= 20) {
            _addAlert('Water level low (${gw.waterLevel}%)', AlertType.warning);
          }
        }
        _lastGateway = gw;
      }
    });

    // Listen to all known zones
    for (final zoneId in FirebasePaths.zoneIds) {
      ref.listen<AsyncValue<ZoneData>>(zoneStreamProvider(zoneId), (previous, next) {
        if (next.hasValue && next.value != null) {
          final zone = next.value!;
          final lastZone = _lastZones[zoneId];

          if (lastZone != null) {
            if (zone.isConnected != lastZone.isConnected) {
              if (zone.isConnected) {
                _addAlert('Zone $zoneId came online', AlertType.info);
              } else {
                _addAlert('Zone $zoneId went offline', AlertType.error);
              }
            }
            if (zone.v != lastZone.v) {
              _addAlert(
                'Zone $zoneId valve ${zone.v ? 'OPENED' : 'CLOSED'}',
                AlertType.info,
              );
            }
          }
          _lastZones[zoneId] = zone;
        }
      });
    }

    return [];
  }

  void _addAlert(String message, AlertType type) {
    state = [
      SystemAlert(message, DateTime.now(), type),
      ...state,
    ];
  }
}

final alertsProvider = NotifierProvider<AlertsNotifier, List<SystemAlert>>(() {
  return AlertsNotifier();
});
