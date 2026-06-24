import 'package:flutter/material.dart';
import 'dashboard_screen.dart';
import 'config_screen.dart';
import 'alerts_screen.dart';
import '../../core/theme/app_colors.dart';
import '../widgets/app_bottom_nav_bar.dart';

class MainScreen extends StatefulWidget {
  const MainScreen({super.key});

  /// Allow child widgets to switch tabs via this static method.
  static void switchTab(BuildContext context, int index) {
    final state = context.findAncestorStateOfType<_MainScreenState>();
    state?._switchTo(index);
  }

  @override
  State<MainScreen> createState() => _MainScreenState();
}

class _MainScreenState extends State<MainScreen> {
  int _currentIndex = 0;

  final List<Widget> _screens = const [
    DashboardScreen(),
    ConfigScreen(),
    AlertsScreen(),
  ];

  void _switchTo(int index) {
    setState(() => _currentIndex = index);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      extendBody: true,
      body: IndexedStack(
        index: _currentIndex,
        children: _screens,
      ),
      bottomNavigationBar: AppBottomNavBar(
        currentIndex: _currentIndex,
        onTap: _switchTo,
      ),
    );
  }
}
