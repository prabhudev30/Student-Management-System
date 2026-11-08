#!/bin/sh
# Cross-build both Windows executables with Zig's bundled Windows SDK.
set -eu
cd "$(dirname "$0")/.."
PYTHON=${PYTHON:-python3}
mkdir -p build
if "$PYTHON" -m ziglang version >/dev/null 2>&1; then
    zigcmd() { "$PYTHON" -m ziglang "$@"; }
elif command -v zig >/dev/null 2>&1; then
    zigcmd() { zig "$@"; }
else
    echo "Install Zig (or the ziglang Python package) to cross-build for Windows." >&2
    exit 1
fi
zigcmd rc /fo build/gui.res gui/app.rc
zigcmd cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
    main.c src/student_core.c -o StudentManagement.exe -s
zigcmd cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
    -I src gui/win32_main.c src/student_core.c build/gui.res -o StudentManagementGUI.exe \
    -Wl,--subsystem,windows -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -lole32 -s
printf 'Built StudentManagement.exe and StudentManagementGUI.exe (x64 Windows).\n'
