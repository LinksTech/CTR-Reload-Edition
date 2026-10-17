// rs_module.c - the registry of the modules (rs_module.h)
//
// The built-in modules are the table below, in the order of the sidebar; a
// module's place in the registry is its page ID (enum RsPageId for the
// built-in ones). Rs_ModulesInit fills the registry once, before the shell
// makes the pages.

#include "reloadstudio.h"
#include <string.h>

static const wchar_t *const k_needsTest[] = { L"test", NULL };

// Track and Character start the game through the page Test in game
// (Rs_TestGameExe, Rs_TestCharacter), so they need it. The icons are those
// the sidebar always had: Folder, BulletedList, Contact, Play.
static const struct RsModuleDef g_builtin[RS_PAGE_BUILTIN_COUNT] = {
    { RS_MODULE_ABI, sizeof(struct RsModuleDef), L"track", L"\xE8B7", 0, k_needsTest, &g_rsTrackPage },
    { RS_MODULE_ABI, sizeof(struct RsModuleDef), L"cups",  L"\xE8FD", 0, NULL,        &g_rsCupsPage },
    { RS_MODULE_ABI, sizeof(struct RsModuleDef), L"char",  L"\xE77B", 0, k_needsTest, &g_rsCharPage },
    { RS_MODULE_ABI, sizeof(struct RsModuleDef), L"test",  L"\xE768", 0, NULL,        &g_rsTestPage },
};

static const struct RsModuleDef *g_modules[RS_MAX_MODULES];
static int g_moduleCount;

void Rs_ModulesInit(void)
{
    int i;
    if (g_moduleCount)
        return;
    for (i = 0; i < RS_PAGE_BUILTIN_COUNT; i++)
        g_modules[g_moduleCount++] = &g_builtin[i];
}

int Rs_ModuleCount(void)
{
    return g_moduleCount;
}

int Rs_ModuleBuiltinCount(void)
{
    return RS_PAGE_BUILTIN_COUNT;
}

const struct RsModuleDef *Rs_ModuleAt(int id)
{
    return (id >= 0 && id < g_moduleCount) ? g_modules[id] : NULL;
}

int Rs_ModuleByWord(const wchar_t *word)
{
    int i;
    if (!word)
        return -1;
    for (i = 0; i < g_moduleCount; i++)
        if (wcscmp(word, g_modules[i]->word) == 0)
            return i;
    return -1;
}
