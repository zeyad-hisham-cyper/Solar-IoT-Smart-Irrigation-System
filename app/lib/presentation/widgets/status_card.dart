import 'package:flutter/material.dart';
import '../../core/theme/app_colors.dart';
import 'glass_card.dart';

/// Prominent status card with a glowing indicator dot, title, subtitle and an
/// optional trailing widget (e.g. the AUTO/MANUAL toggle). Used for the
/// gateway online/offline summary on the dashboard.
class StatusCard extends StatelessWidget {
  final bool online;
  final String title;
  final String subtitle;
  final Widget? trailing;
  final IconData icon;

  const StatusCard({
    super.key,
    required this.online,
    required this.title,
    required this.subtitle,
    this.trailing,
    this.icon = Icons.router_outlined,
  });

  @override
  Widget build(BuildContext context) {
    final statusColor = online ? AppColors.online : AppColors.error;

    return GlassCard(
      padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 16),
      child: Row(
        children: [
          // Icon chip with glow.
          Container(
            padding: const EdgeInsets.all(11),
            decoration: BoxDecoration(
              color: statusColor.withValues(alpha: 0.12),
              borderRadius: BorderRadius.circular(14),
              border: Border.all(color: statusColor.withValues(alpha: 0.25)),
            ),
            child: Icon(icon, color: statusColor, size: 22),
          ),
          const SizedBox(width: 14),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    _Dot(color: statusColor),
                    const SizedBox(width: 8),
                    Flexible(
                      child: Text(
                        title,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                          color: AppColors.textPrimary,
                          fontSize: 15,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 3),
                Text(
                  subtitle,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    color: AppColors.textTertiary,
                    fontSize: 12,
                  ),
                ),
              ],
            ),
          ),
          if (trailing != null) ...[
            const SizedBox(width: 12),
            trailing!,
          ],
        ],
      ),
    );
  }
}

class _Dot extends StatelessWidget {
  final Color color;
  const _Dot({required this.color});

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 9,
      height: 9,
      decoration: BoxDecoration(
        color: color,
        shape: BoxShape.circle,
        boxShadow: [
          BoxShadow(color: color.withValues(alpha: 0.6), blurRadius: 7),
        ],
      ),
    );
  }
}
