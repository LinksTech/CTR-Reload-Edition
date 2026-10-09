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
// typed in, 2 = clicked (Browse, Clear, the slider, automation, a wheel file
// written since it was read). Cleared.
int  CharWheels_ExportChanged(void);
// When Reload Studio is active again or the page is shown again (the page's
// Char_FilesChanged): a file the last char-wheel read (the OBJ or PLY, its MTL,
// its texture, or a file in the folder of a texture not found) was written
// since - the wheel is read again at once and CharWheels_ExportChanged says 2.
// 1 = so; why goes into the automation log.
int  CharWheels_FilesChanged(HWND page, const wchar_t *why);
// Counts every wheel file written since (CharWheels_FilesChanged): the page
// takes it into the stamp of its check while the wheel is passed, so that a
// result kept is not shown for a wheel exported again.
int  CharWheels_Generation(void);
// The textures the last char-wheel did not find: their number, their file
// names into out ("a.png, b.png"; out may be NULL).
int  CharWheels_MissingTextures(wchar_t *out, int cap);
// 1 while the wheel model is in the view (drawn where the kart wheels are
// shown - also on a classic model, which is built without it).
int  CharWheels_Shown(void);
// The page's choices, after every change: an OBJ, Show kart wheels, the
// native model on (ticked or the user mode), the wheel model passed, the axes
// of the card Import (Up Z, Forward -Z; the wheel's as well). The line under
// the card's options follows them; new axes read the wheel again.
void CharWheels_PageState(int obj, int kartWheels, int nativeOn, int passed, int upZ, int backwards);
// The page's Textures folder as make-char gets it (--textures; "" = none): it
// goes to char-wheel as well, and another folder reads the wheel again.
void CharWheels_SetTextures(const wchar_t *dir);
#endif
