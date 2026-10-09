// rs_anim.h - the card "Animations" of the page Character
#ifndef RS_ANIM_H
#define RS_ANIM_H
#include "reloadstudio.h"

// The poses of a driver, in the order of the card's list: the shape keys
// (glTF morph targets) make-char reads by these names (case does not matter).
enum RsAnimPose {
    RS_ANIM_STEER_LEFT = 0,     // animation 0, frames 0..9 (steering left)
    RS_ANIM_STEER_RIGHT,        // animation 0, frames 11..20
    RS_ANIM_REVERSE,            // animation 1, 7 frames
    RS_ANIM_CRASH,              // animation 2, 15 frames
    RS_ANIM_JUMP,               // animation 3, 4 frames
    RS_ANIM_WIN,                // after the finish, places 1-3 (native model)
    RS_ANIM_LOSE,               // after the finish, places 4-8 (native model)
    RS_ANIM_POSES
};

// What make-char said about the poses in a check (the page collects it from
// the lines of the run, rs_char.c Char_ParseLine):
//   @pose <name> from-file|automatic|mirrored|neutral|error [<reason>]
//   @model shape-key <name> <pose|unknown>       every shape key of a glTF
//   @msg warning pose-unknown-key "<name>" ...   a shape key of no pose, ignored
//   @msg warning pose-not-symmetric ...          one steering key, no mirror
//   @msg error model-rig ...                     a glTF with a skin (a rig)
struct RsAnimPoses {
    int seen;                                   // at least one @pose line
    wchar_t state[RS_ANIM_POSES][16];           // "" = not said
    wchar_t why[RS_ANIM_POSES][200];
    int keys;                                   // shape keys of a pose (@model shape-key)
    int unknown;                                // shape keys of no pose, each name once
    wchar_t unknownNames[400];                  // their names, "a, b, c"
    wchar_t symmetric[300];                     // the text of pose-not-symmetric, "" = none
    int rig;                                    // model-rig came
};
void CharAnim_PoseLine(struct RsAnimPoses *poses, wchar_t **f, int n);              // an @pose line, split
void CharAnim_PoseMsg(struct RsAnimPoses *poses, const wchar_t *code, const wchar_t *text);  // every @msg
void CharAnim_ShapeKey(struct RsAnimPoses *poses, const wchar_t *name, const wchar_t *pose);       // @model shape-key

void CharAnim_Create(HWND page, HWND view);                         // after the page's own controls
int  CharAnim_Layout(HWND page, int left, int right, int top, int labelW, int compact);  // adds its card, returns its bottom;
                                                                    // compact: the compact layout of the page (rs_char.c)
int  CharAnim_Command(HWND page, int id, int code);                 // 1 = its control, handled
LRESULT CharAnim_Notify(HWND page, NMHDR *hdr, int *handled);
LRESULT CharAnim_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled);
int  CharAnim_Automate(HWND page, const wchar_t *verb, const wchar_t *arg);  // RS_AUTO_UNKNOWN if not its verb
int  CharAnim_Busy(void);
// After every check: the model ("" = none), its format as rldpack read it
// (@model format: glb, gltf, obj, ply; "" = not said) and what the check said
// about its poses (NULL = nothing).
void CharAnim_ModelChecked(HWND page, const wchar_t *model, const wchar_t *format, const struct RsAnimPoses *poses, int ok);
// After every new preview file in the view: the animation shown goes on.
void CharAnim_PreviewLoaded(void);
// The page's choice Neutral / Steering left / Steering right below the
// preview (0, -10, +10): the card's steering follows (Play stops).
void CharAnim_SteerFromPage(int steer);
// The native model is built (1) or not (0), after every change of the page:
// without it win and lose are not built - their tick boxes are greyed out
// and the list says so.
void CharAnim_NativeState(int native);
void CharAnim_Report(FILE *f);                                      // appended to "report"
#endif
