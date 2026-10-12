#ifndef PLATFORM_NATIVE_MOUSE_H
#define PLATFORM_NATIVE_MOUSE_H

// THE MOUSE IN MENUS (platform/native_mouse.c).
//
// Off until the mouse moves; a key or a pad button hides the pointer and turns
// it off again. Never during a race (NativeControlsPage_RaceContext) and never
// in measuring mode (--deterministic): there every call below answers 0, and no
// menu sees a difference. The pointer is the system cursor with a shape of our
// own; it is not drawn into the picture.

// From the event loop (native_platform.c).
void NativeMouse_OnMotion(float windowX, float windowY);
void NativeMouse_OnButton(int button, int down); // SDL_BUTTON_LEFT / _RIGHT
void NativeMouse_OnWheel(float y);               // + is away from the user
void NativeMouse_OnKeyOrPad(void);               // hides the pointer

// Once per frame, after the events: shows or hides the pointer.
void NativeMouse_Update(void);

// Is the mouse in use in this frame (moved since the last key or pad press,
// in a menu)?
int NativeMouse_Active(void);

// Where the pointer is, in the 512 x 240 space the menus draw in, for an element
// whose frame (same space) is given: the widescreen shift that element gets on
// the canvas is taken back, so the answer can be compared with the coordinates
// the element was drawn with. 0 if the mouse is not active or the pointer is
// outside the picture.
int NativeMouse_MenuPoint(const RECT *element, int *x, int *y);

// Has the pointer moved since the last call? (hover only follows a moving
// pointer, so a resting one does not fight the keys)
int NativeMouse_TakeMoved(void);

// A click (button down) since the last call; 1 left, 2 right.
int NativeMouse_TakeClick(int button);

// Wheel notches since the last call, + away from the user.
int NativeMouse_TakeWheel(void);

#endif
