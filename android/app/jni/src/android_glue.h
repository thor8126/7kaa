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

// Called by Vga::flip() on the final 800x600 frame: draws the temporary touch
// debug cross (see android_touch.cpp).
void android_touch_draw_debug(SDL_Surface *surface);

// Smooth pan / pinch zoom of the map view (android_view.cpp).
// Called each frame by World::detect: scrolls the engine to follow the view.
void android_view_world_hook();
// Called at the start of Sys::disp_zoom: draws the parts of the map the shown
// window needs beyond the game's own view (zoomed out / while panning).
void android_view_disp_zoom_begin();
// Called by Vga::flip() after the frame is copied: draws the zoomed/panned
// map view over it. front = vga_front's surface (8-bit, game palette).
void android_view_render(SDL_Renderer *renderer, SDL_Texture *texture, SDL_Surface *front);

#endif
