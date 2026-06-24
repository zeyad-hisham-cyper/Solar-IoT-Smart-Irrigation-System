import 'dart:math' as math;
import 'package:flutter/material.dart';
import '../../core/theme/app_colors.dart';

/// Animated circular moisture gauge with gradient colors.
class MoistureGauge extends StatelessWidget {
  final int moisturePercent;
  final int threshold;
  final double size;
  final double strokeWidth;
  final bool showThreshold;

  const MoistureGauge({
    super.key,
    required this.moisturePercent,
    this.threshold = 35,
    this.size = 120,
    this.strokeWidth = 10,
    this.showThreshold = true,
  });

  Color get _gaugeColor {
    if (moisturePercent < 20) return AppColors.error;
    if (moisturePercent < 40) return AppColors.warning;
    return AppColors.online;
  }

  Color get _backgroundColor {
    return AppColors.surfaceLight;
  }

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: size,
      height: size,
      child: TweenAnimationBuilder<double>(
        tween: Tween(begin: 0, end: moisturePercent.toDouble()),
        duration: const Duration(milliseconds: 800),
        curve: Curves.easeOutCubic,
        builder: (context, value, child) {
          return CustomPaint(
            painter: _GaugePainter(
              percent: value / 100,
              gaugeColor: _gaugeColor,
              backgroundColor: _backgroundColor,
              strokeWidth: strokeWidth,
              thresholdPercent: showThreshold ? threshold / 100 : null,
            ),
            child: Center(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    '${value.toInt()}%',
                    style: TextStyle(
                      color: AppColors.textPrimary,
                      fontSize: size * 0.22,
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  Text(
                    'Moisture',
                    style: TextStyle(
                      color: AppColors.textTertiary,
                      fontSize: size * 0.1,
                      fontWeight: FontWeight.w500,
                    ),
                  ),
                ],
              ),
            ),
          );
        },
      ),
    );
  }
}

class _GaugePainter extends CustomPainter {
  final double percent;
  final Color gaugeColor;
  final Color backgroundColor;
  final double strokeWidth;
  final double? thresholdPercent;

  _GaugePainter({
    required this.percent,
    required this.gaugeColor,
    required this.backgroundColor,
    required this.strokeWidth,
    this.thresholdPercent,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final radius = (size.width - strokeWidth) / 2;
    const startAngle = -math.pi / 2;
    const fullAngle = 2 * math.pi;

    // Background arc
    final bgPaint = Paint()
      ..color = backgroundColor
      ..strokeWidth = strokeWidth
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;

    canvas.drawCircle(center, radius, bgPaint);

    // Value arc
    final valuePaint = Paint()
      ..color = gaugeColor
      ..strokeWidth = strokeWidth
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;

    canvas.drawArc(
      Rect.fromCircle(center: center, radius: radius),
      startAngle,
      fullAngle * percent,
      false,
      valuePaint,
    );

    // Threshold marker
    if (thresholdPercent != null) {
      final thresholdAngle = startAngle + fullAngle * thresholdPercent!;
      final markerLength = strokeWidth * 1.5;
      final innerRadius = radius - markerLength / 2;
      final outerRadius = radius + markerLength / 2;

      final start = Offset(
        center.dx + innerRadius * math.cos(thresholdAngle),
        center.dy + innerRadius * math.sin(thresholdAngle),
      );
      final end = Offset(
        center.dx + outerRadius * math.cos(thresholdAngle),
        center.dy + outerRadius * math.sin(thresholdAngle),
      );

      final markerPaint = Paint()
        ..color = AppColors.textTertiary
        ..strokeWidth = 2
        ..strokeCap = StrokeCap.round;

      canvas.drawLine(start, end, markerPaint);
    }
  }

  @override
  bool shouldRepaint(covariant _GaugePainter oldDelegate) {
    return percent != oldDelegate.percent ||
        gaugeColor != oldDelegate.gaugeColor ||
        thresholdPercent != oldDelegate.thresholdPercent;
  }
}
