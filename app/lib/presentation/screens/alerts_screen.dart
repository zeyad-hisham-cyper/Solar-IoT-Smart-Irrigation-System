import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:intl/intl.dart';
import '../../providers/alerts_provider.dart';
import '../../core/theme/app_colors.dart';
import '../widgets/alert_card.dart';

class AlertsScreen extends ConsumerWidget {
  const AlertsScreen({super.key});

  IconData _iconForType(AlertType type) {
    switch (type) {
      case AlertType.info:
        return Icons.info_outline_rounded;
      case AlertType.warning:
        return Icons.warning_amber_rounded;
      case AlertType.error:
        return Icons.error_outline_rounded;
    }
  }

  Color _colorForType(AlertType type) {
    switch (type) {
      case AlertType.info:
        return AppColors.neutral; // gray (no green/blue)
      case AlertType.warning:
        return AppColors.warning; // amber
      case AlertType.error:
        return AppColors.error; // red
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final alerts = ref.watch(alertsProvider);

    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        bottom: false,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 20, 16, 8),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  const Text(
                    'System Alerts',
                    style: TextStyle(
                      color: AppColors.textPrimary,
                      fontSize: 24,
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  if (alerts.isNotEmpty)
                    Container(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 12, vertical: 6),
                      decoration: BoxDecoration(
                        color: AppColors.accent.withValues(alpha: 0.14),
                        borderRadius: BorderRadius.circular(20),
                        border: Border.all(
                            color: AppColors.accent.withValues(alpha: 0.3)),
                      ),
                      child: Text(
                        '${alerts.length}',
                        style: const TextStyle(
                          color: AppColors.accent,
                          fontSize: 13,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                    ),
                ],
              ),
            ),
            Expanded(
              child: alerts.isEmpty
                  ? _emptyState()
                  : ListView.separated(
                      padding: const EdgeInsets.fromLTRB(16, 8, 16, 120),
                      itemCount: alerts.length,
                      separatorBuilder: (_, _) => const SizedBox(height: 10),
                      itemBuilder: (context, index) {
                        final alert = alerts[index];
                        return AlertCard(
                          icon: _iconForType(alert.type),
                          accent: _colorForType(alert.type),
                          title: alert.message,
                          timestamp: DateFormat('MMM d · HH:mm:ss')
                              .format(alert.timestamp),
                        );
                      },
                    ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _emptyState() {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Container(
            padding: const EdgeInsets.all(20),
            decoration: BoxDecoration(
              color: AppColors.surfaceLight,
              shape: BoxShape.circle,
              border: Border.all(color: AppColors.glassBorder),
            ),
            child: const Icon(Icons.notifications_off_outlined,
                size: 40, color: AppColors.textTertiary),
          ),
          const SizedBox(height: 16),
          const Text(
            'No recent alerts',
            style: TextStyle(
              color: AppColors.textSecondary,
              fontSize: 16,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 6),
          const Text(
            'System events will appear here',
            style: TextStyle(color: AppColors.textTertiary, fontSize: 13),
          ),
        ],
      ),
    );
  }
}
