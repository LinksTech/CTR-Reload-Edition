#include <platform/native_controls.h>

// KEYBOARD CONTROLS - see include/platform/native_controls.h.
// Skeleton: the stock layout only, no file yet.

void NativeControls_Load(void)
{
}

u16 NativeControls_ReadKeyboard(const bool *keys, int raceContext)
{
	(void)keys;
	(void)raceContext;
	return 0xffff;
}

int NativeControls_Key(int action)
{
	(void)action;
	return 0;
}

void NativeControls_KeyLabel(int scancode, char *text, int size)
{
	(void)scancode;
	if (size > 0)
	{
		text[0] = '\0';
	}
}

const char *NativeControls_ActionLabel(int action)
{
	(void)action;
	return "";
}

int NativeControls_KeyAllowed(int scancode)
{
	(void)scancode;
	return 0;
}

void NativeControls_BeginCapture(void)
{
}

int NativeControls_PollCapture(void)
{
	return NATIVE_CONTROLS_CAPTURE_CANCEL;
}

int NativeControls_Capturing(void)
{
	return 0;
}

int NativeControls_Assign(int action, int scancode, int *swappedWith)
{
	(void)action;
	(void)scancode;
	if (swappedWith != NULL)
	{
		*swappedWith = -1;
	}
	return 0;
}

void NativeControls_ResetDefaults(void)
{
}

int NativeControls_SelfTest(const char *dir)
{
	(void)dir;
	return 1;
}
