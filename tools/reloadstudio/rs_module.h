// rs_module.h - the modules of Reload Studio
//
// For the user Reload Studio is ONE program with one window. Inside, every
// page of the sidebar is a module: a name, a page (struct RsPageDef,
// reloadstudio.h), the modules it needs and the oldest core it runs on. The
// core is everything that is no page: the window and the sidebar, the
// helpers of rs_shell.c, rldpack inside the exe (the checks and the packer),
// the preview (rs_view.c, rs_tex.c) and the version.
//
// The built-in modules - Track, Cups, Character, Test in game - are a table
// in rs_module.c; their order is the order of the sidebar and their page IDs
// (enum RsPageId in reloadstudio.h). Rs_ModulesInit registers them before the
// pages are made; the shell then asks only this registry for names, words,
// icons and pages.

#ifndef RS_MODULE_H
#define RS_MODULE_H

#include <wchar.h>

struct RsPageDef;

// The interface of a module. A change that an older module would read
// wrongly raises RS_MODULE_ABI; a module made for another number is not
// taken.
#define RS_MODULE_ABI 1

// The version of the core as one number: the tools version (CTR_TOOLS_VERSION,
// CMakeLists.txt) major.minor.patch.
#define RS_CORE_VERSION_OF(major, minor, patch) \
    (((unsigned)(major) << 16) | ((unsigned)(minor) << 8) | (unsigned)(patch))

// Pages of all modules together, built-in ones included.
#define RS_MAX_MODULES 16

// A page word: 1 to RS_MODULE_WORD_MAX characters out of a-z, 0-9 and '-'.
#define RS_MODULE_WORD_MAX 15

struct RsModuleDef {
    unsigned abi;                   // RS_MODULE_ABI the module was made for
    unsigned size;                  // sizeof(struct RsModuleDef) of the module
    const wchar_t *word;            // the page's word: automation "page <word>", the setting "page="
    const wchar_t *icon;            // one character of Segoe MDL2 Assets for the sidebar, NULL = a default
    unsigned minCore;               // RS_CORE_VERSION_OF(...) the module needs at least, 0 = any
    const wchar_t *const *needs;    // words of the modules it needs, ending with NULL; NULL = none
    const struct RsPageDef *page;   // the page: its navName is the module's name
};

#endif
