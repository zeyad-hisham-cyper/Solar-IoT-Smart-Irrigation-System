import 'package:flutter/material.dart';
import '../../core/theme/app_colors.dart';
import '../../core/theme/app_theme.dart';

/// Reusable glassmorphism card: rounded corners, subtle gradient fill,
/// hairline border and a soft drop shadow. Single source of truth for the
/// app's card styling so individual screens don't repeat decoration code.
class GlassCard extends StatelessWidget {
  final Widget child;
  final EdgeInsetsGeometry padding;
  final double? radius;
  final Color? borderColor;
  final VoidCallback? onTap;
  final Gradient? gradient;

  const GlassCard({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(16),
    this.radius,
    this.borderColor,
    this.onTap,
    this.gradient,
  });

  @override
  Widget build(BuildContext context) {
    final r = radius ?? AppTheme.cardRadius;
    final card = Container(
      padding: padding,
      decoration: BoxDecoration(
        gradient: gradient ?? AppColors.cardGradient,
        borderRadius: BorderRadius.circular(r),
        border: Border.all(color: borderColor ?? AppColors.glassBorder),
        boxShadow: const [
          BoxShadow(
            color: AppColors.glassShadow,
            blurRadius: 18,
            offset: Offset(0, 8),
          ),
        ],
      ),
      child: child,
    );

    if (onTap == null) return card;
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(r),
        child: card,
      ),
    );
  }
}
