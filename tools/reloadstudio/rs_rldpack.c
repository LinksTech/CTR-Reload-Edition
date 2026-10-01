// rs_rldpack.c - rldpack inside Reload Studio
//
// The same source file as the command-line tool, only its main is called
// Rldpack_Main here. rs_shell.c calls it when the exe starts with
// `--rldpack <arguments>`. So there is ONE file for the author and ONE
// implementation of every check - Reload Studio checks nothing itself, it
// only shows what rldpack says (tools/reloadstudio/reloadstudio.h, the section on
// the rldpack pipe).

#define main Rldpack_Main
#include "../rldpack.c"
