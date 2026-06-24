import 'package:flutter/material.dart';

/// Premium dark palette for the Smart Irrigation dashboard.
///
/// Brief palette:
///   - Primary black : #050606
///   - Light gray    : #D2D2D2
///   - Medium gray   : #A5A5A5
///   - Dark gray     : #2E2F2E
///   - Accent red    : #6C1317
///
/// Rule: NO green anywhere. Status/indicators use red, gray and white only.
class AppColors {
  // --- Raw palette tokens ---------------------------------------------------
  static const Color black = Color(0xFF050606);
  static const Color lightGray = Color(0xFFD2D2D2);
  static const Color mediumGray = Color(0xFFA5A5A5);
  static const Color darkGray = Color(0xFF2E2F2E);
  static const Color accentRed = Color(0xFF6C1317);

  // --- Background layers ----------------------------------------------------
  static const Color background = Color(0xFF050606); // primary black
  static const Color surface = Color(0xFF111212); // lifted black
  static const Color surfaceLight = Color(0xFF1B1C1B); // input / chip fill
  static const Color cardBackground = Color(0xFF121413); // card base

  // --- Glassmorphism / borders / shadows ------------------------------------
  static const Color glassBorder = Color(0x1AFFFFFF); // subtle white hairline
  static const Color glassBorderStrong = Color(0xFF2E2F2E); // dark gray edge
  static const Color glassBackground = Color(0x0DFFFFFF); // frosted overlay
  static const Color glassShadow = Color(0x66000000); // soft deep shadow

  // --- Primary accent (deep red) -------------------------------------------
  static const Color primary = Color(0xFF6C1317);
  static const Color primaryDark = Color(0xFF4A0C0F);
  static const Color primaryBright = Color(0xFF8E1A1F); // for fills on black
  static const Color accent = Color(0xFFB0282E); // brighter red highlight

  // --- Gradients ------------------------------------------------------------
  static const LinearGradient primaryGradient = LinearGradient(
    colors: [Color(0xFF6C1317), Color(0xFFB0282E)],
    begin: Alignment.topLeft,
    end: Alignment.bottomRight,
  );

  static const LinearGradient cardGradient = LinearGradient(
    colors: [Color(0xFF1A1B1A), Color(0xFF101110)],
    begin: Alignment.topLeft,
    end: Alignment.bottomRight,
  );

  static const LinearGradient glassGradient = LinearGradient(
    colors: [Color(0x14FFFFFF), Color(0x05FFFFFF)],
    begin: Alignment.topLeft,
    end: Alignment.bottomRight,
  );

  // --- Status colors (NO GREEN) ---------------------------------------------
  /// Online / healthy → light gray-white (never green).
  static const Color online = Color(0xFFE4E4E4);
  /// Active state (valve open, pump running) → red accent.
  static const Color active = Color(0xFFB0282E);
  /// Warning / stale → warm amber (not green, clearly distinct from red).
  static const Color warning = Color(0xFFC9802E);
  /// Error / offline → red.
  static const Color error = Color(0xFFC0282E);
  static const Color neutral = Color(0xFFA5A5A5);
  /// Rain / informational → cool gray (kept off the green/blue spectrum).
  static const Color rain = Color(0xFFB8BFC4);

  // --- Moisture gauge gradient (low→high, no green) -------------------------
  static const Color moistureLow = Color(0xFFC0282E); // red
  static const Color moistureMid = Color(0xFFC9802E); // amber
  static const Color moistureHigh = Color(0xFFE4E4E4); // light gray-white

  // --- Text -----------------------------------------------------------------
  static const Color textPrimary = Color(0xFFF3F3F3);
  static const Color textSecondary = Color(0xFFD2D2D2);
  static const Color textTertiary = Color(0xFFA5A5A5);
  static const Color textDisabled = Color(0xFF5C5D5C);
}
