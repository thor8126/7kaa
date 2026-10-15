# Seven Kingdoms: Ancient Adversaries — Android

This folder builds the game in this repository as an Android app (APK). It is
the same game as the desktop build: the native code is compiled straight from
`../src` and `../include` (nothing is copied or forked), and the game data is
packaged from `../data`. Single-player only.

## What is in here

| Path | What |
| --- | --- |
| `app/build.gradle` | App build. Downloads SDL2 and OpenAL-soft (pinned versions, SHA-256 checked) into `../../7kaa-android-deps` on first build, packages `../data` as assets. |
| `app/jni/CMakeLists.txt` | Native build. Reads the source lists from `src/Makefile.am`, `src/imgfun/generic/Makefile.am`, `src/openal/Makefile.am` and `src/enet/Makefile.am`, so new game sources are picked up automatically. |
| `app/jni/config.h` | Android equivalent of the configure-generated `include/config.h`. |
| `app/jni/src/android_data.cpp` | First-run unpacking of the game data to internal storage. |
| `app/jni/src/android_touch.cpp` | Touchscreen controls. |
| `app/src/main/java/.../SevenKaaActivity.java` | The activity (extends SDL's `SDLActivity`) with the on-screen buttons. |

The game sources only have two small `#ifdef __ANDROID__` hooks: `src/AM.cpp`
(unpack data before `sys.set_game_dir()`) and `src/OVGA.cpp` (touch events in
`Vga::handle_messages()`). The desktop build is unaffected.

### Differences from the desktop build

- Compiled with clang for ARM64 (x86 optional, for PC emulators), with
  `-fsigned-char` as on the desktop.
- Single-player only: `DISABLE_MULTI_PLAYER` is defined and enet is not used.
  Multiplayer sync relies on x87 floating point (`-mfpmath=387`), which ARM
  does not have, so `HAVE_KNOWN_BUILD` is not defined either.
- No gettext/NLS (English only) and no libcurl.
- Sound through OpenAL-soft's OpenSL ES backend.
- Saves and `CONFIG.DAT` go to the app's private storage (`SDL_GetPrefPath`);
  the game data is unpacked to `files/gamedata/` next to them. Uninstalling the
  app deletes saves.

## Controls

The 800×600 game is scaled to fit the screen with black bars at the sides.

| Touch | Does |
| --- | --- |
| Tap | Left click (select, press buttons) |
| Long press (hold ~0.5 s, the phone vibrates) | Right click: move / attack / order the selected units |
| Two-finger tap | Right click at the first finger |
| Drag with one finger | Left drag: box-select units, drag sliders |
| Drag with two fingers | Scroll the map |
| Tap the mini-map | Jump there (as on the desktop) |
| Back button | Esc |

On-screen buttons in the black bars:

- **Right click** (left side, toggle): while lit, every tap is a right click.
  Handy for giving many orders in a row.
- **Shift** (left side, toggle): holds Shift, e.g. to add units to a selection.
- **Army Ctrl+A** (right side): selects the whole army (same as the Army button
  and Ctrl+A on the desktop).
- **Esc** (right side).

A Bluetooth/USB mouse and keyboard also work, exactly as on the desktop.

## Getting the APK

A ready-built APK is in [`../apk/`](../apk/). Every push that changes the game
or this folder also builds a fresh one on GitHub Actions
(`.github/workflows/android.yml`; download the `7kaa-android-apk` artifact from
the run's page), so nothing heavy has to run on your own PC.

## Building locally

The native build is CPU-heavy. It is capped at 3 parallel compile jobs
(`SEVENKAA_JOBS` in `app/jni/CMakeLists.txt`).

Requirements:

- JDK 17 or 21 (`JAVA_HOME` must point to it)
- Android SDK with: platform `android-35`, build-tools `35.0.0`,
  NDK `27.3.13750724`, CMake `3.31.6`
  (Gradle installs missing ones itself if the licenses are accepted)
- Internet on the first build (Gradle, SDL2 and OpenAL-soft are downloaded)

Tell Gradle where the SDK is, either with `ANDROID_HOME` or with a
`local.properties` file in this folder (not committed):

```
sdk.dir=C:/Users/<you>/AppData/Local/Android/Sdk
```

Then, from Git Bash in this `android/` folder:

```bash
JAVA_HOME="C:\Program Files\Eclipse Adoptium\jdk-21.0.9.10-hotspot" ./gradlew.bat assembleDebug
```

(on Linux/macOS: `./gradlew assembleDebug`). The APK is written to
`app/build/outputs/apk/debug/app-debug.apk`. The first build takes a while
(SDL2, OpenAL-soft and ~300 game files).

Options in `gradle.properties` (or `-P` on the command line):

- `sevenkaa.abis` — ABIs to build, default `arm64-v8a` (practically every
  current phone). Add `x86` for Nox, `x86_64` for the Google emulator or
  `armeabi-v7a` for old 32-bit phones, e.g. `-Psevenkaa.abis=arm64-v8a,x86`.
- `sevenkaa.depsDir` — where SDL2/OpenAL-soft are downloaded,
  default `../../7kaa-android-deps` (next to the repository).

## Installing on a phone

1. On the phone: Settings → About phone → tap *Build number* 7 times, then
   Settings → Developer options → enable *USB debugging*.
2. Connect it by USB and accept the debugging prompt on the phone.
3. Install:

```bash
adb install -r ../apk/7kaa-android-arm64.apk
```

`adb` is in `<sdk>/platform-tools`. Alternatively copy `app-debug.apk` to the
phone and open it there (allow installing from unknown sources).

The first start unpacks ~100 MB of game data, which takes a few seconds on a
black screen; later starts are immediate. Logs: `adb logcat -s 7kaa SDL`.
