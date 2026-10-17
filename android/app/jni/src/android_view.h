// Android: smooth panning and pinch zoom of the map view (android_view.cpp).
// Used by the touch code (android_touch.cpp).

#ifndef ANDROID_VIEW_H
#define ANDROID_VIEW_H

// Display (game screen) coordinates -> engine coordinates. Points inside the
// map view are mapped back through the current zoom/pan, so a touch acts on
// what is displayed under the finger. Returns 1 if (x, y) is in the map view
// while it is zoomed or panned, else leaves it unchanged and returns 0.
int  android_view_to_engine(float *x, float *y);

// Whether (x, y), in display coordinates, is inside the map view.
int  android_view_contains(float x, float y);

// Two-finger gesture: midpoint and finger distance in display pixels.
// begin returns 1 if the gesture controls the map view (it is showing and the
// fingers are on it).
int  android_view_gesture_begin(float midX, float midY, float dist);
void android_view_gesture_move(float midX, float midY, float dist);
void android_view_gesture_end();

void android_view_stop_fling();     // a new touch stops a gliding map
void android_view_animate();        // once per frame: fling and settling

#endif
