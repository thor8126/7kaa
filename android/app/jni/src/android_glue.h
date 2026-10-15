// Android-only glue between the game and the Android platform.
// Only included from game sources inside #ifdef __ANDROID__ blocks.

#ifndef ANDROID_GLUE_H
#define ANDROID_GLUE_H

#include <SDL.h>

// Called first thing in main(). Unpacks the game data from the APK assets to
// internal storage (only when the APK's data changed) and points SKDATA at it.
// Returns 0 on failure.
int android_prepare_game_data();

// Translates touchscreen input into the game's mouse events. Returns non-zero
// if the event was consumed and must not be processed further.
int android_touch_event(SDL_Event *event);

// Called once per Vga::handle_messages() after the event queue is drained;
// handles time based gestures (long press = right click).
void android_touch_update();

#endif
