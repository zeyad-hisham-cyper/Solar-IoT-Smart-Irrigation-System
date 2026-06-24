import 'package:flutter/material.dart';
import '../../core/theme/app_colors.dart';

/// AUTO / MANUAL segmented selector with a sliding red active pill.
class ModeToggle extends StatelessWidget {
  final bool isManual;
  final ValueChanged<bool> onChanged; // true = manual
  final bool compact;

  const ModeToggle({
    super.key,
    required this.isManual,
    required this.onChanged,
    this.compact = false,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(4),
      decoration: BoxDecoration(
        color: AppColors.surfaceLight,
        borderRadius: BorderRadius.circular(30),
        border: Border.all(color: AppColors.glassBorder),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          _segment('AUTO', !isManual, () => onChanged(false)),
          _segment('MANUAL', isManual, () => onChanged(true)),
        ],
      ),
    );
  }

  Widget _segment(String label, bool active, VoidCallback onTap) {
    return GestureDetector(
      onTap: onTap,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 220),
        curve: Curves.easeOut,
        padding: EdgeInsets.symmetric(
          horizontal: compact ? 14 : 20,
          vertical: compact ? 7 : 9,
        ),
        decoration: BoxDecoration(
          gradient: active ? AppColors.primaryGradient : null,
          borderRadius: BorderRadius.circular(26),
          boxShadow: active
              ? [
                  BoxShadow(
                    color: AppColors.accent.withValues(alpha: 0.35),
                    blurRadius: 12,
                    offset: const Offset(0, 4),
                  ),
                ]
              : null,
        ),
        child: Text(
          label,
          style: TextStyle(
            color: active ? Colors.white : AppColors.textTertiary,
            fontSize: compact ? 11 : 12,
            fontWeight: FontWeight.w700,
            letterSpacing: 0.5,
          ),
        ),
      ),
    );
  }
}
