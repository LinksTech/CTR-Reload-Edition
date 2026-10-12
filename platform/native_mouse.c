#include <platform/native_mouse.h>

// THE MOUSE IN MENUS - see include/platform/native_mouse.h.
// Skeleton: the mouse is never active.

void NativeMouse_OnMotion(float windowX, float windowY)
{
	(void)windowX;
	(void)windowY;
}

void NativeMouse_OnButton(int button, int down)
{
	(void)button;
	(void)down;
}

void NativeMouse_OnWheel(float y)
{
	(void)y;
}

void NativeMouse_OnKeyOrPad(void)
{
}

void NativeMouse_Update(void)
{
}

int NativeMouse_Active(void)
{
	return 0;
}

int NativeMouse_MenuPoint(const RECT *element, int *x, int *y)
{
	(void)element;
	(void)x;
	(void)y;
	return 0;
}

int NativeMouse_TakeMoved(void)
{
	return 0;
}

int NativeMouse_TakeClick(int button)
{
	(void)button;
	return 0;
}

int NativeMouse_TakeWheel(void)
{
	return 0;
}
