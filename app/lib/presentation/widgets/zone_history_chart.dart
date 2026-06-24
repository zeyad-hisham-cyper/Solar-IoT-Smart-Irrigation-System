import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:intl/intl.dart';
import '../../core/theme/app_colors.dart';
import '../../models/zone_reading.dart';
import 'glass_card.dart';

/// A dark, premium smooth line chart for a single zone history series
/// (temperature, moisture or water consumption).
///
/// Drives its own loading / empty / error / data states from the provided
/// [AsyncValue] so callers stay declarative. Renders nothing heavier than a
/// [CustomPaint] — no external charting dependency.
class ZoneHistoryChart extends StatelessWidget {
  final String title;
  final IconData icon;
  final Color accent;
  final String unit;
  final AsyncValue<List<ZoneReading>> history;

  /// Extracts the plotted value from a reading.
  final double Function(ZoneReading) selector;

  /// Optional formatter for the headline latest value.
  final String Function(double)? valueFormat;

  const ZoneHistoryChart({
    super.key,
    required this.title,
    required this.icon,
    required this.accent,
    required this.unit,
    required this.history,
    required this.selector,
    this.valueFormat,
  });

  String _fmt(double v) =>
      valueFormat != null ? valueFormat!(v) : v.toStringAsFixed(1);

  @override
  Widget build(BuildContext context) {
    return GlassCard(
      padding: const EdgeInsets.fromLTRB(16, 16, 16, 12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _header(),
          const SizedBox(height: 16),
          SizedBox(
            height: 150,
            width: double.infinity,
            child: history.when(
              data: (readings) => _chartOrEmpty(readings),
              loading: () => const _ChartMessage(
                icon: Icons.hourglass_empty_rounded,
                message: 'Loading history…',
                showSpinner: true,
              ),
              error: (_, _) => const _ChartMessage(
                icon: Icons.error_outline_rounded,
                message: 'Could not load history',
                color: AppColors.error,
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _header() {
    final latest = history.maybeWhen(
      data: (r) => r.isEmpty ? null : selector(r.last),
      orElse: () => null,
    );

    return Row(
      children: [
        Container(
          padding: const EdgeInsets.all(8),
          decoration: BoxDecoration(
            color: accent.withValues(alpha: 0.14),
            borderRadius: BorderRadius.circular(10),
          ),
          child: Icon(icon, color: accent, size: 16),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: Text(
            title,
            style: const TextStyle(
              color: AppColors.textPrimary,
              fontSize: 14,
              fontWeight: FontWeight.w700,
            ),
          ),
        ),
        if (latest != null)
          Text(
            '${_fmt(latest)}$unit',
            style: TextStyle(
              color: accent,
              fontSize: 15,
              fontWeight: FontWeight.w800,
            ),
          ),
      ],
    );
  }

  Widget _chartOrEmpty(List<ZoneReading> readings) {
    if (readings.isEmpty) {
      return const _ChartMessage(
        icon: Icons.show_chart_rounded,
        message: 'No history yet — data will appear shortly',
      );
    }

    final points = readings.map(selector).toList();
    final times = readings.map((r) => r.dateTime).toList();

    return Column(
      children: [
        Expanded(
          child: CustomPaint(
            size: Size.infinite,
            painter: _LineChartPainter(values: points, accent: accent),
          ),
        ),
        const SizedBox(height: 6),
        _axisLabels(times, points),
      ],
    );
  }

  Widget _axisLabels(List<DateTime> times, List<double> points) {
    final df = DateFormat('HH:mm');
    final minV = points.reduce((a, b) => a < b ? a : b);
    final maxV = points.reduce((a, b) => a > b ? a : b);
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(df.format(times.first), style: _axisStyle),
        Text(
          'min ${_fmt(minV)} · max ${_fmt(maxV)}$unit',
          style: _axisStyle,
        ),
        Text(df.format(times.last), style: _axisStyle),
      ],
    );
  }

  static const _axisStyle =
      TextStyle(color: AppColors.textTertiary, fontSize: 10);
}

class _ChartMessage extends StatelessWidget {
  final IconData icon;
  final String message;
  final Color color;
  final bool showSpinner;

  const _ChartMessage({
    required this.icon,
    required this.message,
    this.color = AppColors.textTertiary,
    this.showSpinner = false,
  });

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          if (showSpinner)
            const SizedBox(
              width: 22,
              height: 22,
              child: CircularProgressIndicator(
                strokeWidth: 2,
                color: AppColors.accent,
              ),
            )
          else
            Icon(icon, color: color, size: 26),
          const SizedBox(height: 10),
          Text(
            message,
            textAlign: TextAlign.center,
            style: TextStyle(color: color, fontSize: 12),
          ),
        ],
      ),
    );
  }
}

/// Paints a smooth (monotone-ish) line with a soft gradient area fill and a
/// faint horizontal grid. Handles single-point and flat series safely.
class _LineChartPainter extends CustomPainter {
  final List<double> values;
  final Color accent;

  _LineChartPainter({required this.values, required this.accent});

  @override
  void paint(Canvas canvas, Size size) {
    if (values.isEmpty) return;

    const padTop = 8.0;
    const padBottom = 8.0;
    final chartH = size.height - padTop - padBottom;

    double minV = values.reduce((a, b) => a < b ? a : b);
    double maxV = values.reduce((a, b) => a > b ? a : b);
    if (maxV - minV < 1e-6) {
      // Flat line: center it with a little headroom.
      minV -= 1;
      maxV += 1;
    }

    // Faint grid lines.
    final gridPaint = Paint()
      ..color = AppColors.glassBorder
      ..strokeWidth = 1;
    for (int i = 0; i <= 3; i++) {
      final y = padTop + chartH * (i / 3);
      canvas.drawLine(Offset(0, y), Offset(size.width, y), gridPaint);
    }

    Offset pointAt(int i) {
      final x = values.length == 1
          ? size.width / 2
          : size.width * (i / (values.length - 1));
      final norm = (values[i] - minV) / (maxV - minV);
      final y = padTop + chartH * (1 - norm);
      return Offset(x, y);
    }

    final pts = List.generate(values.length, pointAt);

    // Build a smooth path via Catmull-Rom → cubic Bézier conversion.
    final line = Path()..moveTo(pts.first.dx, pts.first.dy);
    if (pts.length == 1) {
      // Single point: draw a flat segment so something is visible.
      line.lineTo(size.width, pts.first.dy);
    } else {
      for (int i = 0; i < pts.length - 1; i++) {
        final p0 = i == 0 ? pts[i] : pts[i - 1];
        final p1 = pts[i];
        final p2 = pts[i + 1];
        final p3 = i + 2 < pts.length ? pts[i + 2] : p2;
        final c1 = Offset(
          p1.dx + (p2.dx - p0.dx) / 6,
          p1.dy + (p2.dy - p0.dy) / 6,
        );
        final c2 = Offset(
          p2.dx - (p3.dx - p1.dx) / 6,
          p2.dy - (p3.dy - p1.dy) / 6,
        );
        line.cubicTo(c1.dx, c1.dy, c2.dx, c2.dy, p2.dx, p2.dy);
      }
    }

    // Gradient area fill under the line.
    final fill = Path.from(line)
      ..lineTo(pts.last.dx, size.height)
      ..lineTo(pts.first.dx, size.height)
      ..close();
    canvas.drawPath(
      fill,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [
            accent.withValues(alpha: 0.28),
            accent.withValues(alpha: 0.02),
          ],
        ).createShader(Offset.zero & size),
    );

    // The line itself.
    canvas.drawPath(
      line,
      Paint()
        ..color = accent
        ..strokeWidth = 2.4
        ..style = PaintingStyle.stroke
        ..strokeCap = StrokeCap.round
        ..strokeJoin = StrokeJoin.round,
    );

    // Endpoint marker.
    canvas.drawCircle(pts.last, 3.5, Paint()..color = accent);
    canvas.drawCircle(
      pts.last,
      3.5,
      Paint()
        ..color = AppColors.background
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.5,
    );
  }

  @override
  bool shouldRepaint(covariant _LineChartPainter old) =>
      old.values != values || old.accent != accent;
}
