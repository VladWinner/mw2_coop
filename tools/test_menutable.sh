#!/bin/sh
# Builds and runs tools/test_menutable.c in a scratch folder with an override file (mw2coop/menus.menu) next to the program.
set -e
cd "$(dirname "$0")/.."
LLVM_MINGW=${LLVM_MINGW:-../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64}
T=build/menutable-test
mkdir -p "$T/mw2coop"
cat > "$T/mw2coop/menus.menu" <<'MENU'
menuDef { name "main_text"
  itemDef { name "mw2coop_specops_btn" text "Custom join" action { play "mouse_click"; exec "my_command"; setdvar credits 0 } }
  itemDef { name "not_in_builtin" text "x" }
}
menuDef { name "mw2coop_host" title "My title" }
MENU
"$LLVM_MINGW/bin/x86_64-w64-mingw32-clang" -O2 -Wall -Wextra -Isrc -o "$T/test_menutable.exe" tools/test_menutable.c src/menutable.c
"$T/test_menutable.exe"
