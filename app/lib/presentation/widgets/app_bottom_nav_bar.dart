import 'dart:ui';
import 'package:flutter/material.dart';
import '../../core/theme/app_colors.dart';

/// Glass/dark bottom navigation bar with a frosted blur, hairline top border
/// and a red active indicator. Three destinations: Dashboard, Config, Alerts.
class AppBottomNavBar extends StatelessWidget {
  final int currentIndex;
  final ValueChanged<int> onTap;

  const AppBottomNavBar({
    super.key,
    required this.currentIndex,
    required this.onTap,
  });

  static const _items = <_NavItem>[
    _NavItem(Icons.dashboard_outlined, Icons.dashboard_rounded, 'Dashboard'),
    _NavItem(Icons.tune_outlined, Icons.tune_rounded, 'Config'),
    _NavItem(Icons.notifications_none_rounded, Icons.notifications_rounded,
        'Alerts'),
  ];

  @override
  Widget build(BuildContext context) {
    final bottomInset = MediaQuery.of(context).padding.bottom;

    return ClipRRect(
      borderRadius: const BorderRadius.vertical(top: Radius.circular(24)),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 18, sigmaY: 18),
        child: Container(
          padding: EdgeInsets.only(
            top: 10,
            bottom: 10 + bottomInset,
            left: 10,
            right: 10,
          ),
          decoration: BoxDecoration(
            color: AppColors.surface.withValues(alpha: 0.82),
            border: const Border(
              top: BorderSide(color: AppColors.glassBorder),
            ),
            boxShadow: const [
              BoxShadow(
                color: AppColors.glassShadow,
                blurRadius: 24,
                offset: Offset(0, -6),
              ),
            ],
          ),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.spaceAround,
            children: List.generate(_items.length, (i) {
              final item = _items[i];
              final active = i == currentIndex;
              return Expanded(
                child: GestureDetector(
                  behavior: HitTestBehavior.opaque,
                  onTap: () => onTap(i),
                  child: AnimatedContainer(
                    duration: const Duration(milliseconds: 220),
                    curve: Curves.easeOut,
                    margin: const EdgeInsets.symmetric(horizontal: 4),
                    padding: const EdgeInsets.symmetric(vertical: 8),
                    decoration: BoxDecoration(
                      color: active
                          ? AppColors.accent.withValues(alpha: 0.14)
                          : Colors.transparent,
                      borderRadius: BorderRadius.circular(16),
                      border: Border.all(
                        color: active
                            ? AppColors.accent.withValues(alpha: 0.35)
                            : Colors.transparent,
                      ),
                    ),
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(
                          active ? item.activeIcon : item.icon,
                          color: active
                              ? AppColors.accent
                              : AppColors.textTertiary,
                          size: 23,
                        ),
                        const SizedBox(height: 4),
                        Text(
                          item.label,
                          style: TextStyle(
                            color: active
                                ? AppColors.textPrimary
                                : AppColors.textTertiary,
                            fontSize: 11,
                            fontWeight:
                                active ? FontWeight.w700 : FontWeight.w500,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              );
            }),
          ),
        ),
      ),
    );
  }
}

class _NavItem {
  final IconData icon;
  final IconData activeIcon;
  final String label;
  const _NavItem(this.icon, this.activeIcon, this.label);
}
