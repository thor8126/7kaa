// Android: smooth panning and zooming (in and out) of the map view
// (android_view.cpp). Used by the touch code (android_touch.cpp).

#ifndef ANDROID_VIEW_H
#define ANDROID_VIEW_H

// Display (game screen) coordinates -> engine coordinates. A point in the map
// view is mapped back through the current zoom/pan, so a touch acts on what
// is displayed under the finger.
//   returns 0  - not in the map view, or the view is not zoomed/panned:
//                (x, y) unchanged
//   returns 1  - mapped; (x, y) are now engine coordinates
//   returns -1 - in the map view but outside the part the game currently
//                has as its view (zoomed out, or near the edge while
//                panning); call android_view_request() so the hook moves the
//                game's view there, then map again on a later frame
int  android_view_to_engine(float *x, float *y);

// Same, but a point outside the game's view is clamped to its edge (used
// while dragging a selection box or aiming a building).
void android_view_to_engine_clamped(float *x, float *y);

// Move the game's view (by whole locations, without moving the picture) so
// that the display point (x, y) is inside it. Done by the next hook call.
void android_view_request(float x, float y);

// While a one-finger gesture works with the game's view, it must not be moved
// by panning normalisation.
void android_view_set_hold(int hold);

// Whether (x, y), in display coordinates, is inside the map view.
int  android_view_contains(float x, float y);

// Busy drawing extra map passes: the game's view is temporarily elsewhere.
int  android_view_busy();

// Two-finger gesture: midpoint and finger distance in display pixels.
// begin returns 1 if the gesture controls the map view (it is showing and the
// fingers are on it).
int  android_view_gesture_begin(float midX, float midY, float dist);
void android_view_gesture_move(float midX, float midY, float dist);
void android_view_gesture_end();

void android_view_reset_zoom();     // back to the normal view (three-finger tap)
void android_view_stop_fling();     // a new touch stops a gliding map
void android_view_animate();        // once per frame: fling and settling

#endif
