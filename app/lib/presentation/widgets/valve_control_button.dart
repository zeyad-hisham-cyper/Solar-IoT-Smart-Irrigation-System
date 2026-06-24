import 'package:flutter/material.dart';
import '../../core/theme/app_colors.dart';
import 'confirm_dialog.dart';

/// Manual valve control button with confirmation dialog and debounce spinner.
class ValveControlButton extends StatefulWidget {
  final bool isValveOpen;
  final bool isManualMode;
  final bool isGatewayOnline;
  final bool isZoneOnline;
  final int defaultDuration;
  final Future<void> Function(bool open, int duration) onCommand;

  const ValveControlButton({
    super.key,
    required this.isValveOpen,
    required this.isManualMode,
    required this.isGatewayOnline,
    required this.isZoneOnline,
    required this.defaultDuration,
    required this.onCommand,
  });

  @override
  State<ValveControlButton> createState() => _ValveControlButtonState();
}

class _ValveControlButtonState extends State<ValveControlButton> {
  bool _isSending = false;

  bool get _isEnabled =>
      widget.isManualMode &&
      widget.isGatewayOnline &&
      widget.isZoneOnline &&
      !_isSending;

  String get _disabledReason {
    if (!widget.isManualMode) return 'Switch to Manual mode to control';
    if (!widget.isGatewayOnline) {
      return 'Gateway is offline — commands will not be delivered';
    }
    if (!widget.isZoneOnline) return 'Zone is not responding';
    return '';
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const Text(
          'Valve Control',
          style: TextStyle(
            color: AppColors.textSecondary,
            fontSize: 14,
            fontWeight: FontWeight.w600,
          ),
        ),
        const SizedBox(height: 12),
        Row(
          children: [
            Expanded(
              child: _ControlButton(
                label: 'Open Valve',
                icon: Icons.water_drop,
                color: AppColors.active,
                isActive: widget.isValveOpen,
                isEnabled: _isEnabled && !widget.isValveOpen,
                isLoading: _isSending,
                onTap: () => _sendCommand(true),
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: _ControlButton(
                label: 'Close Valve',
                icon: Icons.water_drop_outlined,
                color: AppColors.error,
                isActive: !widget.isValveOpen,
                isEnabled: _isEnabled && widget.isValveOpen,
                isLoading: _isSending,
                onTap: () => _sendCommand(false),
              ),
            ),
          ],
        ),
        if (!_isEnabled && !_isSending)
          Padding(
            padding: const EdgeInsets.only(top: 8),
            child: Text(
              _disabledReason,
              style: const TextStyle(
                color: AppColors.textTertiary,
                fontSize: 11,
                fontStyle: FontStyle.italic,
              ),
            ),
          ),
      ],
    );
  }

  Future<void> _sendCommand(bool open) async {
    final confirmed = await showConfirmDialog(
      context,
      title: open ? 'Open Valve' : 'Close Valve',
      message: open
          ? 'Are you sure you want to open the valve?'
          : 'Are you sure you want to close the valve?',
      detail: open ? 'Duration: ${widget.defaultDuration} seconds' : null,
      confirmColor: open ? AppColors.active : AppColors.error,
      icon: open ? Icons.water_drop : Icons.water_drop_outlined,
      iconColor: open ? AppColors.active : AppColors.error,
    );

    if (!confirmed) return;

    setState(() => _isSending = true);

    try {
      await widget.onCommand(open, open ? widget.defaultDuration : 0);
      // Debounce: disable buttons for 2 seconds
      await Future.delayed(const Duration(seconds: 2));
    } finally {
      if (mounted) setState(() => _isSending = false);
    }
  }
}

class _ControlButton extends StatelessWidget {
  final String label;
  final IconData icon;
  final Color color;
  final bool isActive;
  final bool isEnabled;
  final bool isLoading;
  final VoidCallback onTap;

  const _ControlButton({
    required this.label,
    required this.icon,
    required this.color,
    required this.isActive,
    required this.isEnabled,
    required this.isLoading,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: isEnabled ? onTap : null,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 200),
        padding: const EdgeInsets.symmetric(vertical: 16),
        decoration: BoxDecoration(
          color: isActive
              ? color.withValues(alpha: 0.1)
              : AppColors.surfaceLight.withValues(
                  alpha: isEnabled ? 1.0 : 0.5),
          borderRadius: BorderRadius.circular(12),
          border: Border.all(
            color: isActive
                ? color.withValues(alpha: 0.3)
                : AppColors.glassBorder.withValues(
                    alpha: isEnabled ? 1.0 : 0.3),
          ),
        ),
        child: Column(
          children: [
            isLoading
                ? SizedBox(
                    width: 22,
                    height: 22,
                    child: CircularProgressIndicator(
                      strokeWidth: 2,
                      color: color,
                    ),
                  )
                : Icon(
                    icon,
                    color: isEnabled || isActive
                        ? color
                        : AppColors.textDisabled,
                    size: 22,
                  ),
            const SizedBox(height: 6),
            Text(
              label,
              style: TextStyle(
                color: isEnabled || isActive
                    ? AppColors.textPrimary
                    : AppColors.textDisabled,
                fontSize: 12,
                fontWeight: FontWeight.w600,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
