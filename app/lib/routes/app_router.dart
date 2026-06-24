
import 'package:go_router/go_router.dart';
import '../presentation/screens/main_screen.dart';
import '../presentation/screens/zone_detail_screen.dart';

final appRouter = GoRouter(
  initialLocation: '/',
  routes: [
    GoRoute(
      path: '/',
      builder: (context, state) => const MainScreen(),
    ),
    GoRoute(
      path: '/zone/:zoneId',
      builder: (context, state) {
        final zoneId = state.pathParameters['zoneId']!;
        return ZoneDetailScreen(zoneId: zoneId);
      },
    ),
  ],
);
