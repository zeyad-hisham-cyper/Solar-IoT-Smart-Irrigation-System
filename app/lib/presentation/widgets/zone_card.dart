import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/theme/app_colors.dart';
import '../../core/utils/timestamp_utils.dart';
import '../../models/zone_data.dart';
import '../../core/constants/firebase_paths.dart';
import '../../providers/command_provider.dart';
import 'moisture_gauge.dart';
import 'glass_card.dart';

/// Zone summary card for the dashboard grid.
class ZoneCard extends ConsumerWidget {
  final String zoneId;
  final ZoneData zone;
  final VoidCallback onTap;
  final bool isManual;

  const ZoneCard({
    super.key,
    required this.zoneId,
    required this.zone,
    required this.onTap,
    required this.isManual,
  });

  Color get _statusColor {
    if (!zone.isConnected) return AppColors.error;
    if (TimestampUtils.isZoneStale(zone.lu, zoneId)) return AppColors.warning;
    return AppColors.online;
  }

  String get _statusLabel {
    if (!zone.isConnected) return 'Offline';
    if (TimestampUtils.isZoneStale(zone.lu, zoneId)) return 'Stale';
    return 'Online';
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final zoneName = FirebasePaths.zoneNames[zoneId] ?? zoneId.toUpperCase();

    return GlassCard(
      onTap: onTap,
      padding: const EdgeInsets.all(14),
      child: Opacity(
        opacity: zone.isConnected ? 1.0 : 0.55,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            // Header: name + online/offline indicator
            Row(
              children: [
                Expanded(
                  child: Text(
                    zoneName,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      color: AppColors.textPrimary,
                      fontSize: 15,
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                ),
                Container(
                  width: 9,
                  height: 9,
                  decoration: BoxDecoration(
                    color: _statusColor,
                    shape: BoxShape.circle,
                    boxShadow: [
                      BoxShadow(
                        color: _statusColor.withValues(alpha: 0.5),
                        blurRadius: 6,
                      ),
                    ],
                  ),
                ),
              ],
            ),
            Text(
              _statusLabel,
              style: TextStyle(
                color: _statusColor,
                fontSize: 10,
                fontWeight: FontWeight.w600,
              ),
            ),
            const SizedBox(height: 8),

            // Moisture circular indicator
            Center(
              child: MoistureGauge(
                moisturePercent: zone.sm,
                threshold: zone.th,
                size: 84,
                strokeWidth: 7,
              ),
            ),
            const SizedBox(height: 10),

            // Temperature + valve status
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                _miniStat(
                  Icons.thermostat_rounded,
                  '${zone.t.toStringAsFixed(1)}°C',
                  AppColors.warning,
                ),
                _valveBadge(),
              ],
            ),
            const SizedBox(height: 6),

            // Threshold + last update
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Flexible(
                  child: Text(
                    'Th ${zone.th}%',
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      color: AppColors.textTertiary,
                      fontSize: 10,
                    ),
                  ),
                ),
                Flexible(
                  child: Text(
                    TimestampUtils.timeAgo(zone.lu),
                    overflow: TextOverflow.ellipsis,
                    textAlign: TextAlign.right,
                    style: TextStyle(
                      color: _statusColor,
                      fontSize: 10,
                      fontWeight: FontWeight.w500,
                    ),
                  ),
                ),
              ],
            ),

            if (isManual && zone.isConnected) ...[
              const SizedBox(height: 10),
              SizedBox(
                width: double.infinity,
                child: ElevatedButton(
                  onPressed: () async {
                    final repo = ref.read(commandRepositoryProvider);
                    if (!zone.v) {
                      await repo.openValve(zoneId, durationSeconds: zone.dur);
                    } else {
                      await repo.closeValve(zoneId);
                    }
                  },
                  style: ElevatedButton.styleFrom(
                    backgroundColor:
                        zone.v ? AppColors.error : AppColors.primary,
                    foregroundColor: Colors.white,
                    padding: const EdgeInsets.symmetric(vertical: 9),
                    elevation: 0,
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(10),
                    ),
                  ),
                  child: Text(
                    zone.v ? 'CLOSE VALVE' : 'OPEN VALVE',
                    style: const TextStyle(
                      fontSize: 11,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }

  Widget _miniStat(IconData icon, String text, Color color) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, color: color, size: 15),
        const SizedBox(width: 3),
        Text(
          text,
          style: const TextStyle(
            color: AppColors.textSecondary,
            fontSize: 12,
            fontWeight: FontWeight.w600,
          ),
        ),
      ],
    );
  }

  Widget _valveBadge() {
    final open = zone.v;
    final color = open ? AppColors.active : AppColors.textTertiary;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: open
            ? AppColors.active.withValues(alpha: 0.14)
            : AppColors.surfaceLight,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(
          color: open
              ? AppColors.active.withValues(alpha: 0.3)
              : AppColors.glassBorder,
        ),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(
            open ? Icons.water_drop_rounded : Icons.water_drop_outlined,
            size: 11,
            color: color,
          ),
          const SizedBox(width: 3),
          Text(
            open ? 'Open' : 'Closed',
            style: TextStyle(
              color: color,
              fontSize: 10,
              fontWeight: FontWeight.w700,
            ),
          ),
        ],
      ),
    );
  }
}
