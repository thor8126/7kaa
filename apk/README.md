# Seven Kingdoms — Android APK

**[7kaa-android.apk](7kaa-android.apk)** is the latest Android build of this
repository. It is rebuilt and replaced automatically every time the game or the
Android project changes (`BUILD_INFO.txt` says which commit it was built from).

The same file is always available at one fixed link, handy to bookmark on the
phone:

https://github.com/thor8126/7kaa/releases/download/android-latest/7kaa-android.apk

## Install

- **On the phone:** open the link above (or tap `7kaa-android.apk` here, then
  *Download*), open the downloaded file, and allow installing unknown apps
  when Android asks.
- **From a PC over USB** (USB debugging on):
  `adb install -r 7kaa-android.apk`

Installing a newer build over an older one keeps your saved games. (If the
phone says *App not installed* when updating from a build made before
2026-10-17, uninstall the old app once; from then on updates install over each
other.)

It needs a 64-bit ARM phone (practically every phone from the last ~8 years)
running Android 7.0 or newer. The first start unpacks the game data, which
takes a few seconds on a black screen.

Controls and build details: [android/README.md](../android/README.md).
