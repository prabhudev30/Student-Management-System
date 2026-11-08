# Student Management Studio

A fully offline student-record manager with a **native Windows desktop GUI written in C/Win32** and a separate, still-supported C console application. Both front ends use the same validated student-record engine in `src/student_core.c`.

The GUI opens as a real Windows window with a dashboard, sidebar, searchable/sortable student table, forms, attendance, academic reports, CSV tools, and backup/restore. It does not require a browser, server, account, or internet connection.

## Windows quick start

1. Download and extract the complete **Student Management System Windows ZIP** into a folder.
2. Double-click **`START_GUI_WINDOWS.bat`** (or open `StudentManagementGUI.exe`).
3. Your records are stored under `%APPDATA%\StudentManagementSystem\students.csv` and are saved automatically after successful changes.

`START_WINDOWS.bat` remains available for the independent console version. The executables are separate:

- `StudentManagementGUI.exe` — native graphical desktop app (Windows x64).
- `StudentManagement.exe` — original menu-driven console app, retained and rebuilt from the same shared core.

## GUI features

- Responsive dashboard with aligned statistics cards, custom sidebar/actions, Segoe UI typography, and per-monitor DPI handling (100%, 125%, and 150% scaling).
- Sidebar navigation for Dashboard, Students, and Rankings; table headings sort records, and the directory search matches roll, name, or class.
- Add, edit, view, and confirmed delete forms; roll numbers are assigned automatically.
- Automatic grade, pass/fail, and rankings; attendance recording and class/individual summary reporting.
- CSV import/merge, report export, named backup, validated restore, and automatic save backups.
- Existing `students.csv` records are copied into the user-data folder only when that destination does not already exist. If the user-data database already exists and a console/legacy source is found beside the app, the GUI offers a one-time additive merge: new roll numbers are added, existing rolls are never overwritten, and source files are preserved. If no local source is found, a one-time prompt can open a file picker for a console CSV in another folder. This recovers console records missing from an older GUI database without re-importing them on every launch. If no CSV exists, a legacy `students.txt` is migrated without deleting or changing it.
- Offline local storage in the Windows user profile; no external service or network access.

## Build on Windows (MinGW-w64)

Install a MinGW-w64 toolchain that includes GCC and `windres`, open a command prompt in the project folder, then run:

```bat
BUILD_WINDOWS.bat
```

The build produces both Windows executables. To build the console version only, run `mingw32-make` or compile directly:

```bat
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic main.c src\student_core.c -o StudentManagement.exe
```

## Cross-build Windows binaries from Linux/macOS

Install Zig (or the `ziglang` Python package), then run:

```sh
sh scripts/build_windows.sh
```

The script embeds the common-controls manifest and produces `StudentManagement.exe` and `StudentManagementGUI.exe`. The repository includes ready-to-run x64 Windows builds as well.

## Build and use the console app on Linux/macOS

```sh
make
make run
```

Or compile directly:

```sh
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic main.c src/student_core.c -o StudentManagement
./StudentManagement
```

The console app retains its original workflows and supports:

| Option | Action |
|---|---|
| 1 | Add student with automatic roll, grade, and result |
| 2 | View sorted records |
| 3 | Search by roll, name, or class |
| 4 | Update a student |
| 5 | Delete after confirmation |
| 6 | Record individual/class attendance and review low attendance |
| 7 | Summary, class summaries, ranking, and report card |
| 8 | Export report CSV |
| 9 | Import/merge CSV |
| 10 | Create backup |
| 11 | Validate and restore backup |
| 0 | Exit |

Command-line options remain available: `StudentManagement --help`, `--summary`, and `--data FILE`. A custom `--data` path does not import the legacy text file.

## Data compatibility and migration

The canonical CSV schema is unchanged:

```csv
roll,name,class,marks,attended,total
1,Asha Devi,10 A,95.50,19,20
```

The GUI prioritizes an existing database in `%APPDATA%\StudentManagementSystem`. If none exists, it copies a working-folder `students.csv` into that directory without altering the original. If no CSV is available, it looks for a legacy `students.txt` in the working folder, then in the user-data folder; migration only happens when the destination CSV is missing. The original legacy file is preserved. Existing user-data records are never overwritten by migration.

Console migration continues to use its existing current-folder `students.csv` and `students.txt` conventions. Empty-but-existing databases are respected; removed records are not re-imported on later runs.

`attended=0,total=0` means attendance has not been recorded. Grades: A+ ≥90, A ≥80, B ≥70, C ≥60, D ≥50, E ≥40, F <40. The pass mark is 40; low attendance is below 75%. Equal marks share a rank, with following rank positions skipped.

## CSV, automatic saving, and recovery

- Both the six-column storage CSV and the nine-column report CSV are accepted for import; calculated report fields are recomputed.
- Import validates the complete file before saving, skips existing rolls rather than overwriting students, and assigns rolls for blank-roll rows.
- Each successful save writes a complete temporary file before replacement and keeps the previous database as an automatic `.bak` file.
- Restore validates the backup before changing live data and saves a copy of the current database first.
- Names/classes are CSV-escaped when exported. UTF-8 BOM and Windows CRLF are accepted on import.

## Tests

With GCC and Python 3 installed:

```sh
make test
```

`make test` currently runs 31 tests: all 28 original console tests, shared-core CRUD/attendance/CSV/backup/restore/legacy-merge operations, and two source-level GUI regression checks for DPI, layout hooks, Unicode handling, and additive migration. Static GUI checks are not a substitute for Windows runtime testing. Tests use isolated temporary directories and do not modify the included student data.

## Project files

- `main.c` — preserved console front end.
- `src/student_core.h`, `src/student_core.c` — shared portable record and storage engine.
- `gui/win32_main.c` — native Win32 graphical application.
- `gui/app.manifest`, `gui/app.rc` — common-controls and DPI manifest resource.
- `START_GUI_WINDOWS.bat`, `START_WINDOWS.bat` — GUI and console launchers.
- `BUILD_WINDOWS.bat`, `scripts/build_windows.sh`, `Makefile` — build workflows.
- `tests/` — original console tests and shared-core operation tests.
- `docs/GUI_USER_GUIDE.md` — concise Windows workflow and data-safety guide.

## Windows testing note

The GUI and console targets were cross-compiled as x64 Windows PE executables and statically checked in this environment. **A Windows desktop/runtime was not available here, so I could not launch or interactively test the GUI on Windows.** Cross-compilation is not a substitute for that runtime test; please run `START_GUI_WINDOWS.bat` on Windows and report any issue before treating the GUI as fully verified.
