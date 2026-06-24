import 'package:flutter/material.dart';
import '../../core/theme/app_colors.dart';
import 'glass_card.dart';

/// Temperature card showing ambient temperature and rain status.
class WeatherWidget extends StatelessWidget {
  final num temperature;
  final bool isRaining;

  const WeatherWidget({
    super.key,
    required this.temperature,
    required this.isRaining,
  });

  @override
  Widget build(BuildContext context) {
    final accent = isRaining ? AppColors.rain : AppColors.warning;

    return GlassCard(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            children: [
              Container(
                padding: const EdgeInsets.all(9),
                decoration: BoxDecoration(
                  color: accent.withValues(alpha: 0.14),
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Icon(
                  isRaining ? Icons.water_drop_rounded : Icons.wb_sunny_rounded,
                  color: accent,
                  size: 18,
                ),
              ),
              const Spacer(),
              if (isRaining)
                Container(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                  decoration: BoxDecoration(
                    color: AppColors.rain.withValues(alpha: 0.14),
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: const Text(
                    'RAIN',
                    style: TextStyle(
                      color: AppColors.rain,
                      fontSize: 10,
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                ),
            ],
          ),
          const SizedBox(height: 14),
          Text(
            '${temperature.toStringAsFixed(1)}°C',
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(
              color: AppColors.textPrimary,
              fontSize: 22,
              fontWeight: FontWeight.w800,
            ),
          ),
          const SizedBox(height: 2),
          Text(
            isRaining ? 'Temperature · Raining' : 'Temperature · Clear',
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(
              color: AppColors.textTertiary,
              fontSize: 12,
              fontWeight: FontWeight.w500,
            ),
          ),
        ],
      ),
    );
  }
}
