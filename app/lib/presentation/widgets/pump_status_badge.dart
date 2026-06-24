import 'package:flutter/material.dart';
import '../../core/theme/app_colors.dart';
import 'glass_card.dart';

/// Water pump on/off status card. Red accent when running.
class PumpStatusBadge extends StatelessWidget {
  final bool isOn;
  final VoidCallback? onTap;

  const PumpStatusBadge({
    super.key,
    required this.isOn,
    this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final accent = isOn ? AppColors.active : AppColors.neutral;

    return GlassCard(
      onTap: onTap,
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            children: [
              AnimatedContainer(
                duration: const Duration(milliseconds: 300),
                padding: const EdgeInsets.all(9),
                decoration: BoxDecoration(
                  color: accent.withValues(alpha: 0.14),
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Icon(Icons.water_rounded, color: accent, size: 18),
              ),
              const Spacer(),
              AnimatedContainer(
                duration: const Duration(milliseconds: 300),
                width: 9,
                height: 9,
                decoration: BoxDecoration(
                  color: accent,
                  shape: BoxShape.circle,
                  boxShadow: isOn
                      ? [
                          BoxShadow(
                            color: accent.withValues(alpha: 0.5),
                            blurRadius: 6,
                          ),
                        ]
                      : null,
                ),
              ),
            ],
          ),
          const SizedBox(height: 14),
          Text(
            isOn ? 'Running' : 'Off',
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(
              color: AppColors.textPrimary,
              fontSize: 22,
              fontWeight: FontWeight.w800,
            ),
          ),
          const SizedBox(height: 2),
          Row(
            children: [
              const Expanded(
                child: Text(
                  'Water Pump',
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: AppColors.textTertiary,
                    fontSize: 12,
                    fontWeight: FontWeight.w500,
                  ),
                ),
              ),
              if (onTap != null)
                const Icon(Icons.touch_app_rounded,
                    size: 13, color: AppColors.textTertiary),
            ],
          ),
        ],
      ),
    );
  }
}
