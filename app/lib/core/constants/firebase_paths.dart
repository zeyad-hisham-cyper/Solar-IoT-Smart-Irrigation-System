/// All Firebase Realtime Database path constants.
/// Derived from the firmware's firebase_sync.c implementation.
class FirebasePaths {
  static const String gatewayId = 'g1';

  // Gateway
  static String gateway() => 'gateways/$gatewayId';
  static String gatewayMode() => 'gateways/$gatewayId/mode';

  // Zones
  static String zone(String zoneId) => 'zones/$zoneId';
  static String zoneThreshold(String zoneId) => 'zones/$zoneId/th';
  static String zoneDuration(String zoneId) => 'zones/$zoneId/dur';

  // Zone history (time-series readings, added beside existing latest values)
  // zones/{zoneId}/history/{readingId}
  static String zoneHistory(String zoneId) => 'zones/$zoneId/history';

  // Commands
  static String commands() => 'commands/$gatewayId';
  static String commandPump() => 'commands/$gatewayId/pump';
  static String commandZoneValve(String zoneId) =>
      'commands/$gatewayId/z/$zoneId/v';
  static String commandZoneDuration(String zoneId) =>
      'commands/$gatewayId/z/$zoneId/d';
  static String commandTimestamp() => 'commands/$gatewayId/ts';

  // Weather
  static String weather() => 'weather';

  // Known zone IDs
  static const List<String> zoneIds = ['z1', 'z2'];

  // Zone display names
  static const Map<String, String> zoneNames = {
    'z1': 'Zone 1',
    'z2': 'Zone 2',
  };
}
