/// Zone command model — represents a valve command for a specific zone.
/// Firebase path: commands/g1/z/z1, commands/g1/z/z2
class ZoneCommand {
  final bool v;    // Valve command: true = open
  final int d;     // Duration in seconds (0 = use zone default)

  const ZoneCommand({
    required this.v,
    required this.d,
  });

  factory ZoneCommand.fromJson(Map<String, dynamic> json) {
    return ZoneCommand(
      v: (json['v'] as bool?) ?? false,
      d: (json['d'] as num?)?.toInt() ?? 0,
    );
  }

  Map<String, dynamic> toJson() {
    return {
      'v': v,
      'd': d,
    };
  }
}
