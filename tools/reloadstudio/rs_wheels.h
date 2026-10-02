// rs_wheels.h - the card "Wheels" of the page Character (preview feature)
#ifndef RS_WHEELS_H
#define RS_WHEELS_H
#include "reloadstudio.h"
void CharWheels_Create(HWND page, HWND view);                         // after the page's own controls
int  CharWheels_Layout(HWND page, int left, int right, int top, int labelW);  // adds its card, returns its bottom
int  CharWheels_Command(HWND page, int id, int code);                 // 1 = its control, handled
LRESULT CharWheels_Notify(HWND page, NMHDR *hdr, int *handled);
LRESULT CharWheels_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled);
int  CharWheels_Automate(HWND page, const wchar_t *verb, const wchar_t *arg);  // RS_AUTO_UNKNOWN if not its verb
int  CharWheels_Busy(void);
void CharWheels_ModelChecked(HWND page, const wchar_t *model, int sizePercent, int ok);  // after every check, "" = no model
void CharWheels_Report(FILE *f);                                      // appended to "report"
#endif
