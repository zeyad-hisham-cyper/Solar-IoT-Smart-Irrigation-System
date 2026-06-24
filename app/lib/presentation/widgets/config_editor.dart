import 'dart:async';
import 'package:flutter/material.dart';
import '../../core/theme/app_colors.dart';
import '../../core/constants/app_constants.dart';

/// Zone configuration editor with threshold slider and duration input.
class ConfigEditor extends StatefulWidget {
  final int currentThreshold;
  final int currentDuration;
  final Future<void> Function(int threshold) onThresholdSaved;
  final Future<void> Function(int duration) onDurationSaved;

  /// When true the editor renders without its own card chrome (header + outer
  /// container) so it can be embedded inside an existing card.
  final bool embedded;

  const ConfigEditor({
    super.key,
    required this.currentThreshold,
    required this.currentDuration,
    required this.onThresholdSaved,
    required this.onDurationSaved,
    this.embedded = false,
  });

  @override
  State<ConfigEditor> createState() => _ConfigEditorState();
}

class _ConfigEditorState extends State<ConfigEditor> {
  late double _threshold;
  late TextEditingController _durationController;
  Timer? _thresholdDebounce;
  Timer? _durationDebounce;
  bool _thresholdSaving = false;
  bool _durationSaving = false;

  @override
  void initState() {
    super.initState();
    _threshold = widget.currentThreshold.toDouble();
    _durationController =
        TextEditingController(text: widget.currentDuration.toString());
  }

  @override
  void didUpdateWidget(ConfigEditor oldWidget) {
    super.didUpdateWidget(oldWidget);
    // Only update if the user isn't currently editing
    if (_thresholdDebounce == null && !_thresholdSaving) {
      _threshold = widget.currentThreshold.toDouble();
    }
    if (_durationDebounce == null && !_durationSaving) {
      _durationController.text = widget.currentDuration.toString();
    }
  }

  @override
  void dispose() {
    _durationController.dispose();
    _thresholdDebounce?.cancel();
    _durationDebounce?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final content = Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
          // Header
          if (!widget.embedded) ...[
            const Row(
              children: [
                Icon(Icons.tune, color: AppColors.accent, size: 18),
                SizedBox(width: 8),
                Text(
                  'Zone Configuration',
                  style: TextStyle(
                    color: AppColors.textPrimary,
                    fontSize: 15,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 20),
          ],

          // Threshold slider
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              const Text(
                'Moisture Threshold',
                style: TextStyle(
                  color: AppColors.textSecondary,
                  fontSize: 13,
                  fontWeight: FontWeight.w500,
                ),
              ),
              Row(
                children: [
                  Text(
                    '${_threshold.toInt()}%',
                    style: const TextStyle(
                      color: AppColors.accent,
                      fontSize: 14,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                  if (_thresholdSaving) ...[
                    const SizedBox(width: 8),
                    const SizedBox(
                      width: 12,
                      height: 12,
                      child: CircularProgressIndicator(
                        strokeWidth: 2,
                        color: AppColors.accent,
                      ),
                    ),
                  ],
                ],
              ),
            ],
          ),
          const SizedBox(height: 6),
          Slider(
            value: _threshold,
            min: AppConstants.thresholdMin.toDouble(),
            max: AppConstants.thresholdMax.toDouble(),
            divisions: 100,
            label: '${_threshold.toInt()}%',
            onChanged: (value) {
              setState(() => _threshold = value);
              _debounceThresholdSave(value.toInt());
            },
          ),
          const SizedBox(height: 16),

          // Duration input
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              const Text(
                'Irrigation Duration',
                style: TextStyle(
                  color: AppColors.textSecondary,
                  fontSize: 13,
                  fontWeight: FontWeight.w500,
                ),
              ),
              if (_durationSaving)
                const SizedBox(
                  width: 12,
                  height: 12,
                  child: CircularProgressIndicator(
                    strokeWidth: 2,
                    color: AppColors.accent,
                  ),
                ),
            ],
          ),
          const SizedBox(height: 8),
          Row(
            children: [
              Expanded(
                child: TextField(
                  controller: _durationController,
                  keyboardType: TextInputType.number,
                  style: const TextStyle(
                    color: AppColors.textPrimary,
                    fontSize: 14,
                  ),
                  decoration: InputDecoration(
                    hintText: 'Seconds (1-3600)',
                    suffixText: 'sec',
                    suffixStyle: const TextStyle(
                      color: AppColors.textTertiary,
                      fontSize: 13,
                    ),
                    contentPadding: const EdgeInsets.symmetric(
                        horizontal: 16, vertical: 12),
                    border: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(12),
                    ),
                  ),
                  onChanged: (value) {
                    final intVal = int.tryParse(value);
                    if (intVal != null &&
                        intVal >= AppConstants.durationMin &&
                        intVal <= AppConstants.durationMax) {
                      _debounceDurationSave(intVal);
                    }
                  },
                ),
              ),
            ],
          ),
        ],
    );

    if (widget.embedded) return content;

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.cardBackground,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: AppColors.glassBorder),
      ),
      child: content,
    );
  }

  void _debounceThresholdSave(int value) {
    _thresholdDebounce?.cancel();
    _thresholdDebounce = Timer(
      const Duration(milliseconds: AppConstants.configDebounceMsec),
      () async {
        setState(() => _thresholdSaving = true);
        await widget.onThresholdSaved(value);
        if (mounted) setState(() => _thresholdSaving = false);
      },
    );
  }

  void _debounceDurationSave(int value) {
    _durationDebounce?.cancel();
    _durationDebounce = Timer(
      const Duration(milliseconds: AppConstants.configDebounceMsec),
      () async {
        setState(() => _durationSaving = true);
        await widget.onDurationSaved(value);
        if (mounted) setState(() => _durationSaving = false);
      },
    );
  }
}
