import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';
import '../../core/theme/app_colors.dart';
import '../../core/constants/firebase_paths.dart';
import '../../core/utils/timestamp_utils.dart';
import '../../providers/gateway_provider.dart';
import '../../providers/zone_provider.dart';
import '../../providers/weather_provider.dart';
import '../../providers/connectivity_provider.dart';
import '../../providers/command_provider.dart';
import '../widgets/status_card.dart';
import '../widgets/mode_toggle.dart';
import '../widgets/zone_card.dart';
import '../widgets/weather_widget.dart';
import '../widgets/pump_status_badge.dart';
import '../widgets/glass_card.dart';
import '../widgets/confirm_dialog.dart';

/// Main dashboard screen with system overview.
class DashboardScreen extends ConsumerWidget {
  const DashboardScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final hasConnection = ref.watch(hasConnectionProvider);
    final gatewayAsync = ref.watch(gatewayStreamProvider);
    final isOnline = ref.watch(gatewayOnlineProvider);
    final weatherAsync = ref.watch(weatherStreamProvider);
    final isManual = ref.watch(isManualModeProvider);

    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        bottom: false,
        child: !hasConnection
            ? _buildNoConnectionOverlay()
            : RefreshIndicator(
                color: AppColors.accent,
                backgroundColor: AppColors.surface,
                onRefresh: () async {
                  ref.invalidate(gatewayStreamProvider);
                  for (final zoneId in FirebasePaths.zoneIds) {
                    ref.invalidate(zoneStreamProvider(zoneId));
                  }
                  ref.invalidate(weatherStreamProvider);
                },
                child: ListView(
                  physics: const AlwaysScrollableScrollPhysics(),
                  padding: const EdgeInsets.fromLTRB(16, 8, 16, 120),
                  children: [
                    _header(),
                    const SizedBox(height: 20),

                    // Gateway status card
                    gatewayAsync.when(
                      data: (gw) => StatusCard(
                        online: isOnline,
                        title: isOnline ? 'Gateway Online' : 'Gateway Offline',
                        subtitle:
                            'Last seen: ${TimestampUtils.timeAgo(gw.lastSeen)}',
                      ),
                      loading: () => _shimmerCard(height: 76),
                      error: (_, _) => const StatusCard(
                        online: false,
                        title: 'Gateway Offline',
                        subtitle: 'No data received',
                      ),
                    ),
                    const SizedBox(height: 14),

                    // AUTO / MANUAL mode selector
                    _modeSelector(ref, isManual),
                    const SizedBox(height: 14),

                    // Temperature + Water pump cards
                    IntrinsicHeight(
                      child: Row(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          Expanded(
                            child: weatherAsync.when(
                              data: (weather) => WeatherWidget(
                                temperature: weather.temp,
                                isRaining: weather.rain,
                              ),
                              loading: () => _shimmerCard(height: 96),
                              error: (_, _) => const WeatherWidget(
                                temperature: 0,
                                isRaining: false,
                              ),
                            ),
                          ),
                          const SizedBox(width: 12),
                          Expanded(
                            child: gatewayAsync.when(
                              data: (gw) => PumpStatusBadge(
                                isOn: gw.pump,
                                onTap: isManual
                                    ? () => _togglePump(context, ref, gw.pump)
                                    : null,
                              ),
                              loading: () => _shimmerCard(height: 96),
                              error: (_, _) =>
                                  const PumpStatusBadge(isOn: false),
                            ),
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(height: 26),

                    // Section title
                    _sectionTitle('Irrigation Zones',
                        '${FirebasePaths.zoneIds.length} zones'),
                    const SizedBox(height: 14),

                    // Zone cards grid
                    LayoutBuilder(
                      builder: (context, constraints) {
                        final twoCols = constraints.maxWidth > 360;
                        return GridView.builder(
                          shrinkWrap: true,
                          physics: const NeverScrollableScrollPhysics(),
                          gridDelegate:
                              SliverGridDelegateWithFixedCrossAxisCount(
                            crossAxisCount: twoCols ? 2 : 1,
                            childAspectRatio: twoCols ? 0.72 : 1.4,
                            crossAxisSpacing: 12,
                            mainAxisSpacing: 12,
                          ),
                          itemCount: FirebasePaths.zoneIds.length,
                          itemBuilder: (context, index) {
                            final zoneId = FirebasePaths.zoneIds[index];
                            final zoneAsync =
                                ref.watch(zoneStreamProvider(zoneId));

                            return zoneAsync.when(
                              data: (zone) => ZoneCard(
                                zoneId: zoneId,
                                zone: zone,
                                isManual: isManual,
                                onTap: () => context.push('/zone/$zoneId'),
                              ),
                              loading: () => _shimmerCard(height: 200),
                              error: (err, _) =>
                                  _buildErrorCard(err.toString()),
                            );
                          },
                        );
                      },
                    ),
                  ],
                ),
              ),
      ),
    );
  }

  Widget _header() {
    return Padding(
      padding: const EdgeInsets.only(top: 8),
      child: Row(
        children: [
          Container(
            padding: const EdgeInsets.all(11),
            decoration: BoxDecoration(
              gradient: AppColors.primaryGradient,
              borderRadius: BorderRadius.circular(14),
              boxShadow: [
                BoxShadow(
                  color: AppColors.accent.withValues(alpha: 0.4),
                  blurRadius: 14,
                  offset: const Offset(0, 6),
                ),
              ],
            ),
            child: const Icon(Icons.water_drop_rounded,
                color: Colors.white, size: 22),
          ),
          const SizedBox(width: 14),
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: const [
              Text(
                'Smart Irrigation',
                style: TextStyle(
                  color: AppColors.textPrimary,
                  fontSize: 20,
                  fontWeight: FontWeight.w800,
                  letterSpacing: 0.2,
                ),
              ),
              Text(
                'System Dashboard',
                style: TextStyle(
                  color: AppColors.textTertiary,
                  fontSize: 12,
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _modeSelector(WidgetRef ref, bool isManual) {
    return GlassCard(
      padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 14),
      child: Row(
        children: [
          Container(
            padding: const EdgeInsets.all(9),
            decoration: BoxDecoration(
              color: AppColors.accent.withValues(alpha: 0.14),
              borderRadius: BorderRadius.circular(12),
            ),
            child: Icon(
              isManual ? Icons.touch_app_rounded : Icons.auto_mode_rounded,
              color: AppColors.accent,
              size: 18,
            ),
          ),
          const SizedBox(width: 12),
          const Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Operating Mode',
                  style: TextStyle(
                    color: AppColors.textPrimary,
                    fontSize: 14,
                    fontWeight: FontWeight.w700,
                  ),
                ),
                Text(
                  'Switch between automatic and manual control',
                  style: TextStyle(
                    color: AppColors.textTertiary,
                    fontSize: 11,
                  ),
                  overflow: TextOverflow.ellipsis,
                ),
              ],
            ),
          ),
          const SizedBox(width: 10),
          ModeToggle(
            isManual: isManual,
            compact: true,
            onChanged: (manual) async {
              final repo = ref.read(gatewayRepositoryProvider);
              await repo.setMode(manual ? 'manual' : 'auto');
            },
          ),
        ],
      ),
    );
  }

  Widget _sectionTitle(String title, String trailing) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(
          title,
          style: const TextStyle(
            color: AppColors.textPrimary,
            fontSize: 17,
            fontWeight: FontWeight.w800,
          ),
        ),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
          decoration: BoxDecoration(
            color: AppColors.surfaceLight,
            borderRadius: BorderRadius.circular(20),
            border: Border.all(color: AppColors.glassBorder),
          ),
          child: Text(
            trailing,
            style: const TextStyle(
              color: AppColors.textTertiary,
              fontSize: 11,
              fontWeight: FontWeight.w600,
            ),
          ),
        ),
      ],
    );
  }

  Future<void> _togglePump(
      BuildContext context, WidgetRef ref, bool currentState) async {
    final newState = !currentState;
    final confirmed = await showConfirmDialog(
      context,
      title: newState ? 'Turn Pump On' : 'Turn Pump Off',
      message: newState
          ? 'Are you sure you want to turn on the water pump?'
          : 'Are you sure you want to turn off the water pump?',
      confirmColor: newState ? AppColors.accent : AppColors.error,
      icon: Icons.water,
      iconColor: newState ? AppColors.accent : AppColors.error,
    );

    if (!confirmed) return;
    final repo = ref.read(commandRepositoryProvider);
    await repo.setPump(newState);
  }

  Widget _buildNoConnectionOverlay() {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: const [
          Icon(Icons.wifi_off_rounded, size: 64, color: AppColors.textTertiary),
          SizedBox(height: 16),
          Text(
            'No Internet Connection',
            style: TextStyle(
              color: AppColors.textPrimary,
              fontSize: 18,
              fontWeight: FontWeight.w700,
            ),
          ),
          SizedBox(height: 8),
          Text(
            'Check your network settings and try again',
            style: TextStyle(color: AppColors.textTertiary, fontSize: 14),
          ),
        ],
      ),
    );
  }

  Widget _shimmerCard({required double height}) {
    return Container(
      height: height,
      decoration: BoxDecoration(
        color: AppColors.surfaceLight.withValues(alpha: 0.5),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: AppColors.glassBorder),
      ),
    );
  }

  Widget _buildErrorCard(String error) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.error.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: AppColors.error.withValues(alpha: 0.3)),
      ),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: const [
          Icon(Icons.error_outline, color: AppColors.error, size: 32),
          SizedBox(height: 8),
          Text(
            'Error loading zone',
            style: TextStyle(
              color: AppColors.error,
              fontSize: 13,
              fontWeight: FontWeight.w600,
            ),
          ),
        ],
      ),
    );
  }
}
