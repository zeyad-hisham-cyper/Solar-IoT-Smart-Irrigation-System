import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/theme/app_colors.dart';
import '../../core/constants/firebase_paths.dart';
import '../../providers/gateway_provider.dart';
import '../../providers/zone_provider.dart';
import '../widgets/config_editor.dart';
import '../widgets/glass_card.dart';
import '../widgets/mode_toggle.dart';

class ConfigScreen extends ConsumerWidget {
  const ConfigScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final mode = ref.watch(systemModeProvider);
    final isManual = mode == 'manual';

    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        bottom: false,
        child: ListView(
          padding: const EdgeInsets.fromLTRB(16, 12, 16, 120),
          children: [
            const Padding(
              padding: EdgeInsets.only(top: 8, bottom: 4),
              child: Text(
                'Configuration',
                style: TextStyle(
                  color: AppColors.textPrimary,
                  fontSize: 24,
                  fontWeight: FontWeight.w800,
                ),
              ),
            ),
            const SizedBox(height: 16),

            // System Mode Section
            _sectionLabel('SYSTEM MODE'),
            const SizedBox(height: 10),
            GlassCard(
              padding:
                  const EdgeInsets.symmetric(horizontal: 18, vertical: 16),
              child: Row(
                children: [
                  Container(
                    padding: const EdgeInsets.all(10),
                    decoration: BoxDecoration(
                      color: AppColors.accent.withValues(alpha: 0.14),
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: Icon(
                      isManual
                          ? Icons.touch_app_rounded
                          : Icons.auto_mode_rounded,
                      color: AppColors.accent,
                      size: 20,
                    ),
                  ),
                  const SizedBox(width: 14),
                  const Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          'Operating Mode',
                          style: TextStyle(
                            color: AppColors.textPrimary,
                            fontSize: 15,
                            fontWeight: FontWeight.w700,
                          ),
                        ),
                        SizedBox(height: 2),
                        Text(
                          'AUTO follows sensors · MANUAL is hands-on',
                          style: TextStyle(
                            color: AppColors.textTertiary,
                            fontSize: 11,
                          ),
                          overflow: TextOverflow.ellipsis,
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(width: 8),
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
            ),
            const SizedBox(height: 26),

            // Per-Zone Configuration
            _sectionLabel('PER-ZONE CONFIGURATION'),
            const SizedBox(height: 10),

            ...FirebasePaths.zoneIds.map((zoneId) {
              final zoneAsync = ref.watch(zoneStreamProvider(zoneId));

              return Padding(
                padding: const EdgeInsets.only(bottom: 16),
                child: GlassCard(
                  child: zoneAsync.when(
                    data: (zone) => Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            Container(
                              width: 8,
                              height: 8,
                              decoration: const BoxDecoration(
                                color: AppColors.accent,
                                shape: BoxShape.circle,
                              ),
                            ),
                            const SizedBox(width: 10),
                            Text(
                              FirebasePaths.zoneNames[zoneId] ??
                                  zoneId.toUpperCase(),
                              style: const TextStyle(
                                color: AppColors.textPrimary,
                                fontSize: 16,
                                fontWeight: FontWeight.w800,
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 14),
                        ConfigEditor(
                          embedded: true,
                          currentThreshold: zone.th,
                          currentDuration: zone.dur,
                          onThresholdSaved: (th) async {
                            final repo = ref.read(zoneRepositoryProvider);
                            await repo.setThreshold(zoneId, th);
                          },
                          onDurationSaved: (dur) async {
                            final repo = ref.read(zoneRepositoryProvider);
                            await repo.setDuration(zoneId, dur);
                          },
                        ),
                      ],
                    ),
                    loading: () => const SizedBox(
                      height: 120,
                      child: Center(
                        child: CircularProgressIndicator(
                            color: AppColors.accent),
                      ),
                    ),
                    error: (e, _) => Text(
                      'Error loading $zoneId',
                      style: const TextStyle(color: AppColors.error),
                    ),
                  ),
                ),
              );
            }),
          ],
        ),
      ),
    );
  }

  Widget _sectionLabel(String text) {
    return Text(
      text,
      style: const TextStyle(
        color: AppColors.textTertiary,
        fontSize: 12,
        fontWeight: FontWeight.w700,
        letterSpacing: 1.2,
      ),
    );
  }
}
