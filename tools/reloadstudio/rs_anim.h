// rs_anim.h - the card "Animations" of the page Character (preview feature)
#ifndef RS_ANIM_H
#define RS_ANIM_H
#include "reloadstudio.h"
void CharAnim_Create(HWND page, HWND view);                         // after the page's own controls
int  CharAnim_Layout(HWND page, int left, int right, int top, int labelW, int compact);  // adds its card, returns its bottom;
                                                                    // compact: the compact layout of the page (rs_char.c)
int  CharAnim_Command(HWND page, int id, int code);                 // 1 = its control, handled
LRESULT CharAnim_Notify(HWND page, NMHDR *hdr, int *handled);
LRESULT CharAnim_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled);
int  CharAnim_Automate(HWND page, const wchar_t *verb, const wchar_t *arg);  // RS_AUTO_UNKNOWN if not its verb
int  CharAnim_Busy(void);
void CharAnim_ModelChecked(HWND page, const wchar_t *model, int sizePercent, int ok);  // after every check, "" = no model
void CharAnim_Report(FILE *f);                                      // appended to "report"
#endif
