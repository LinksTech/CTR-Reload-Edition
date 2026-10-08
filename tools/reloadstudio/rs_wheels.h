// rs_wheels.h - the card "Wheels" of the page Character (preview feature)
#ifndef RS_WHEELS_H
#define RS_WHEELS_H
#include "reloadstudio.h"
void CharWheels_Create(HWND page, HWND view);                         // after the page's own controls
int  CharWheels_Layout(HWND page, int left, int right, int top, int labelW, int compact);  // adds its card, returns its bottom;
                                                                    // compact: the compact layout of the page (rs_char.c)
int  CharWheels_Command(HWND page, int id, int code);                 // 1 = its control, handled
LRESULT CharWheels_Notify(HWND page, NMHDR *hdr, int *handled);
LRESULT CharWheels_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled);
int  CharWheels_Automate(HWND page, const wchar_t *verb, const wchar_t *arg);  // RS_AUTO_UNKNOWN if not its verb
int  CharWheels_Busy(void);
void CharWheels_ModelChecked(HWND page, const wchar_t *model, int sizePercent, int ok);  // after every check, "" = no model
void CharWheels_Report(FILE *f);                                      // appended to "report"
// The export (rs_char.c, Char_MakeArgs): the wheel model chosen (1, its path
// into out) or none (0); its size in percent (100 = the game's wheel).
int  CharWheels_ModelPath(wchar_t *out, int cap);
int  CharWheels_SizePercent(void);
// 0 = nothing of the export changed since the last call; 1 = the field was
// typed in, 2 = clicked (Browse, Clear, the slider, automation). Cleared.
int  CharWheels_ExportChanged(void);
// The page's choices, after every change: an OBJ, Show kart wheels, the
// native model on (ticked or the user mode), the wheel model passed, the axes
// of the card Import (Up Z, Forward -Z; the wheel's as well). The line under
// the card's options follows them; new axes read the wheel again.
void CharWheels_PageState(int obj, int kartWheels, int nativeOn, int passed, int upZ, int backwards);
#endif
