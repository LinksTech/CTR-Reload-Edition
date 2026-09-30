// am_rldpack.c - rldpack inside the Alpha-Maker
//
// The same source file as the command-line tool, only its main is called
// Rldpack_Main here. am_shell.c calls it when the exe starts with
// `--rldpack <arguments>`. So there is ONE file for the author and ONE
// implementation of every check - the Alpha-Maker checks nothing itself, it
// only shows what rldpack says (tools/alphamaker/alphamaker.h, the section on
// the rldpack pipe).

#define main Rldpack_Main
#include "../rldpack.c"
