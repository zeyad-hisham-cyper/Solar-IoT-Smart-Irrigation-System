import 'package:firebase_core/firebase_core.dart' show FirebaseOptions;
import 'package:flutter/foundation.dart'
    show defaultTargetPlatform, TargetPlatform;

/// Manual Firebase configuration extracted from google-services.json.
/// This replaces the FlutterFire CLI generated file.
class DefaultFirebaseOptions {
  static FirebaseOptions get currentPlatform {
    switch (defaultTargetPlatform) {
      case TargetPlatform.android:
        return android;
      case TargetPlatform.iOS:
        return ios;
      default:
        throw UnsupportedError(
          'DefaultFirebaseOptions are not supported for this platform.',
        );
    }
  }

  static const FirebaseOptions android = FirebaseOptions(
    apiKey: 'AIzaSyAQPKeIHVHg84MT-o8IubbtsL4laHQH88A',
    appId: '1:810760456693:android:ff6cc4b3637b9664d578bc',
    messagingSenderId: '810760456693',
    projectId: 'smart-irrigation-system-e415d',
    databaseURL:
        'https://smart-irrigation-system-e415d-default-rtdb.europe-west1.firebasedatabase.app',
    storageBucket: 'smart-irrigation-system-e415d.firebasestorage.app',
  );

  // iOS config — update these values if you add an iOS app to Firebase
  static const FirebaseOptions ios = FirebaseOptions(
    apiKey: 'AIzaSyAQPKeIHVHg84MT-o8IubbtsL4laHQH88A',
    appId: '1:810760456693:android:ff6cc4b3637b9664d578bc',
    messagingSenderId: '810760456693',
    projectId: 'smart-irrigation-system-e415d',
    databaseURL:
        'https://smart-irrigation-system-e415d-default-rtdb.europe-west1.firebasedatabase.app',
    storageBucket: 'smart-irrigation-system-e415d.firebasestorage.app',
    iosBundleId: 'smart.irrigation',
  );
}
