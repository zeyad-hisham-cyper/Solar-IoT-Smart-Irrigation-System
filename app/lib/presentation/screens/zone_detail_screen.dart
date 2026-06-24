import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';
import '../../core/theme/app_colors.dart';
import '../../core/constants/firebase_paths.dart';
import '../../core/utils/timestamp_utils.dart';
import '../../models/zone_data.dart';
import '../../providers/gateway_provider.dart';
import '../../providers/zone_provider.dart';
import '../../providers/command_provider.dart';
import '../../providers/history_provider.dart';
import '../widgets/moisture_gauge.dart';
import '../widgets/valve_control_button.dart';
import '../widgets/config_editor.dart';
import '../widgets/stale_data_banner.dart';
import '../widgets/metric_card.dart';
import '../widgets/glass_card.dart';
import '../widgets/zone_history_chart.dart';

/// Zone detail screen with full sensor data, controls, configuration and
/// historical analytics graphs.
class ZoneDetailScreen extends ConsumerWidget {
  final String zoneId;

  const ZoneDetailScreen({super.key, required this.zoneId});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final zoneAsync = ref.watch(zoneStreamProvider(zoneId));
    final historyAsync = ref.watch(zoneHistoryProvider(zoneId));
    final isManual = ref.watch(isManualModeProvider);
    final isGatewayOnline = ref.watch(gatewayOnlineProvider);
    final zoneName = FirebasePaths.zoneNames[zoneId] ?? zoneId.toUpperCase();

    // Persist a history reading whenever fresh zone telemetry arrives.
    // De-duplication and throttling live inside the repository, so this is
    // safe to call on every update and never overwrites existing records.
    ref.listen<AsyncValue<ZoneData>>(zoneStreamProvider(zoneId), (_, next) {
      if (!next.hasValue || next.value == null) return;
      final zone = next.value!;
      ref.read(historyRepositoryProvider).recordReadingIfNeeded(
            zoneId,
            temperature: zone.t,
            moisture: zone.sm,
            waterConsumption: zone.f,
            sourceLu: zone.lu,
          );
    });

    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: zoneAsync.when(
          data: (zone) {
            final isZoneStale = TimestampUtils.isZoneStale(zone.lu, zoneId);
            final isZoneOffline =
                TimestampUtils.isZoneOffline(zone.lu, zoneId);

            return ListView(
              padding: const EdgeInsets.fromLTRB(16, 8, 16, 32),
              children: [
                _header(context, zoneName, isZoneOffline),
                const SizedBox(height: 16),

                // Connectivity banner
                if (isZoneOffline)
                  const Padding(
                    padding: EdgeInsets.only(bottom: 14),
                    child: StaleDataBanner(
                      message: 'Zone is offline — no recent data received',
                      isError: true,
                    ),
                  )
                else if (isZoneStale)
                  const Padding(
                    padding: EdgeInsets.only(bottom: 14),
                    child: StaleDataBanner(
                      message: 'Data may be stale — last update was delayed',
                    ),
                  ),

                // Main moisture gauge card
                GlassCard(
                  padding: const EdgeInsets.all(24),
                  child: Column(
                    children: [
                      MoistureGauge(
                        moisturePercent: zone.sm,
                        threshold: zone.th,
                        size: 168,
                        strokeWidth: 14,
                      ),
                      const SizedBox(height: 8),
                      Container(
                        padding: const EdgeInsets.symmetric(
                            horizontal: 12, vertical: 5),
                        decoration: BoxDecoration(
                          color: AppColors.surfaceLight,
                          borderRadius: BorderRadius.circular(20),
                        ),
                        child: Text(
                          'Threshold ${zone.th}%',
                          style: const TextStyle(
                            color: AppColors.textSecondary,
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 12),

                // Temperature / Flow / Valve metric cards
                Row(
                  children: [
                    Expanded(
                      child: MetricCard(
                        icon: Icons.thermostat_rounded,
                        label: 'Temperature',
                        value: '${zone.t.toStringAsFixed(1)}°C',
                        accent: AppColors.warning,
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: MetricCard(
                        icon: Icons.water_drop_rounded,
                        label: 'Flow',
                        value: '${zone.f.toStringAsFixed(1)} mL',
                        accent: AppColors.rain,
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: MetricCard(
                        icon: zone.v
                            ? Icons.lock_open_rounded
                            : Icons.lock_outline_rounded,
                        label: 'Valve',
                        value: zone.v ? 'Open' : 'Closed',
                        accent:
                            zone.v ? AppColors.active : AppColors.neutral,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 12),

                // Last updated row
                _lastUpdatedRow(zone.lu, isZoneStale, isZoneOffline),
                const SizedBox(height: 22),

                // Analytics section
                _sectionLabel('ANALYTICS'),
                const SizedBox(height: 12),
                ZoneHistoryChart(
                  title: 'Temperature History',
                  icon: Icons.thermostat_rounded,
                  accent: AppColors.warning,
                  unit: '°C',
                  history: historyAsync,
                  selector: (r) => r.temperature.toDouble(),
                ),
                const SizedBox(height: 12),
                ZoneHistoryChart(
                  title: 'Moisture History',
                  icon: Icons.opacity_rounded,
                  accent: AppColors.accent,
                  unit: '%',
                  history: historyAsync,
                  selector: (r) => r.moisture.toDouble(),
                  valueFormat: (v) => v.toStringAsFixed(0),
                ),
                const SizedBox(height: 12),
                ZoneHistoryChart(
                  title: 'Water Consumption History',
                  icon: Icons.water_rounded,
                  accent: AppColors.rain,
                  unit: ' mL',
                  history: historyAsync,
                  selector: (r) => r.waterConsumption.toDouble(),
                ),
                const SizedBox(height: 22),

                // Valve control section
                _sectionLabel('CONTROLS'),
                const SizedBox(height: 12),
                GlassCard(
                  child: ValveControlButton(
                    isValveOpen: zone.v,
                    isManualMode: isManual,
                    isGatewayOnline: isGatewayOnline,
                    isZoneOnline: !isZoneOffline,
                    defaultDuration: zone.dur,
                    onCommand: (open, duration) async {
                      final repo = ref.read(commandRepositoryProvider);
                      if (open) {
                        await repo.openValve(zoneId, durationSeconds: duration);
                      } else {
                        await repo.closeValve(zoneId);
                      }
                    },
                  ),
                ),
                const SizedBox(height: 22),

                // Zone configuration section
                _sectionLabel('CONFIGURATION'),
                const SizedBox(height: 12),
                GlassCard(
                  child: ConfigEditor(
                    embedded: true,
                    currentThreshold: zone.th,
                    currentDuration: zone.dur,
                    onThresholdSaved: (th) async {
                      final repo = ref.read(zoneRepositoryProvider);
                      await repo.setThreshold(zoneId, th);
                    },
                    onDurationSaved: (dur) async {
                      final repo = ref.read(zoneRepositoryProvider);
                      await repo.setDuration(zoneId, dur);
                    },
                  ),
                ),
              ],
            );
          },
          loading: () => const Center(
            child: CircularProgressIndicator(color: AppColors.accent),
          ),
          error: (err, _) => _errorState(context, err.toString()),
        ),
      ),
    );
  }

  Widget _header(BuildContext context, String zoneName, bool offline) {
    return Padding(
      padding: const EdgeInsets.only(top: 4),
      child: Row(
        children: [
          // Back button
          GestureDetector(
            onTap: () => context.pop(),
            child: Container(
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                color: AppColors.surfaceLight,
                borderRadius: BorderRadius.circular(14),
                border: Border.all(color: AppColors.glassBorder),
              ),
              child: const Icon(Icons.arrow_back_ios_new_rounded,
                  color: AppColors.textPrimary, size: 18),
            ),
          ),
          const SizedBox(width: 14),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  zoneName,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    color: AppColors.textPrimary,
                    fontSize: 22,
                    fontWeight: FontWeight.w800,
                  ),
                ),
                const Text(
                  'Zone details & analytics',
                  style:
                      TextStyle(color: AppColors.textTertiary, fontSize: 12),
                ),
              ],
            ),
          ),
          // Online/offline pill
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 7),
            decoration: BoxDecoration(
              color: (offline ? AppColors.error : AppColors.online)
                  .withValues(alpha: 0.14),
              borderRadius: BorderRadius.circular(20),
              border: Border.all(
                color: (offline ? AppColors.error : AppColors.online)
                    .withValues(alpha: 0.3),
              ),
            ),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Container(
                  width: 7,
                  height: 7,
                  decoration: BoxDecoration(
                    color: offline ? AppColors.error : AppColors.online,
                    shape: BoxShape.circle,
                  ),
                ),
                const SizedBox(width: 6),
                Text(
                  offline ? 'Offline' : 'Online',
                  style: TextStyle(
                    color: offline ? AppColors.error : AppColors.online,
                    fontSize: 11,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _lastUpdatedRow(int lu, bool isStale, bool isOffline) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      decoration: BoxDecoration(
        color: AppColors.surfaceLight,
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: AppColors.glassBorder),
      ),
      child: Row(
        children: [
          const Icon(Icons.access_time_rounded,
              color: AppColors.textTertiary, size: 16),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              'Last updated: ${TimestampUtils.timeAgo(lu)}',
              style: TextStyle(
                color: isStale ? AppColors.warning : AppColors.textSecondary,
                fontSize: 13,
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _sectionLabel(String text) {
    return Text(
      text,
      style: const TextStyle(
        color: AppColors.textTertiary,
        fontSize: 12,
        fontWeight: FontWeight.w700,
        letterSpacing: 1.2,
      ),
    );
  }

  Widget _errorState(BuildContext context, String message) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const Icon(Icons.error_outline_rounded,
                color: AppColors.error, size: 48),
            const SizedBox(height: 16),
            const Text(
              'Error loading zone data',
              style: TextStyle(
                color: AppColors.textPrimary,
                fontSize: 16,
                fontWeight: FontWeight.w700,
              ),
            ),
            const SizedBox(height: 8),
            Text(
              message,
              textAlign: TextAlign.center,
              style:
                  const TextStyle(color: AppColors.textTertiary, fontSize: 13),
            ),
            const SizedBox(height: 20),
            OutlinedButton.icon(
              onPressed: () => context.pop(),
              icon: const Icon(Icons.arrow_back_rounded, size: 18),
              label: const Text('Go back'),
            ),
          ],
        ),
      ),
    );
  }
}
