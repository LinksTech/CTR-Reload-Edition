# Turns menus/nitro-pit.menu into the built-in stock.
#
# WHY GENERATED AND NOT COPIED. The game should start without the file,
# and it should use it when it exists. Both together mean that
# the declaration has to exist twice - as a file and as a string in the image.
# Two copies drift apart; a copy made by the build does not.

file(READ "${MENU}" TEXT)

# In this order: the backslash first, otherwise the escape characters of the
# later steps would be escaped once more.
string(REPLACE "\\" "\\\\" TEXT "${TEXT}")
string(REPLACE "\"" "\\\"" TEXT "${TEXT}")
string(REPLACE "\r" "" TEXT "${TEXT}")

# Every line becomes a string literal of its own, and EVERY one needs its
# continuation - without the backslash at the end of the line the macro ends after the
# first line, and the rest of the file stands as bare text in the image.
string(REPLACE "\n" "\\n\" \\\n    \"" TEXT "${TEXT}")

file(WRITE "${HEADER}"
"// GENERATED FROM ${MENU} - DO NOT EDIT BY HAND.\n"
"//\n"
"// The built-in stock of the menu declaration. It is character for character\n"
"// the file it comes from, so that the declaration exists exactly once.\n"
"#ifndef CTR_NATIVE_MENUDEFAULT_H\n"
"#define CTR_NATIVE_MENUDEFAULT_H\n"
"\n"
"#define NATIVE_MENU_DEFAULT_TEXT \\\n"
"    \"${TEXT}\"\n"
"\n"
"#endif\n"
)
