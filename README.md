# Automatic Student Management System

A complete, menu-driven **C console project** for managing all student records. It upgrades the original add/view starter into a persistent record manager with automatic roll numbers, grades, results, attendance percentages, rankings, statistics, CSV import/export, and backup/restore.

Your original `students.txt` records are preserved. On the first normal run, the program automatically migrates them into `students.csv`; later runs use the CSV database.

## Features

- **Automatic roll numbers** for new students (highest existing roll + 1)
- **Full CRUD:** add, view, search, update, and confirmed delete
- **Full names and classes/sections**, including names containing spaces
- **Automatic grades and pass/fail results** from marks out of 100
- **Attendance:** individual sessions, whole-class sessions, corrections, and low-attendance reports
- **Automatic reports:** class average, highest/lowest marks, pass percentage, grade counts, attendance percentage, class summaries, rank lists, and individual report cards
- **Sorting and case-insensitive search** by roll, name, or class
- **CSV export** with calculated grade, result, and attendance columns
- **CSV import/merge** with validation, duplicate-roll skipping, and optional automatic roll assignment
- **Automatic saving and backup:** every confirmed change is saved; the previous database is kept as `students.csv.bak`
- **Manual backup and validated restore**
- **Safe file handling:** complete temporary files are written before replacement; failed saves roll back in-memory changes; corrupt databases stop startup instead of being overwritten
- **Input validation:** no unsafe `scanf` loops, overlong-line recovery, range checks, duplicate checks, and safe EOF exit

## Quick start

### Linux / macOS

```sh
make run
```

Or compile directly:

```sh
cc -std=c11 -O2 -Wall -Wextra -Wpedantic main.c -o StudentManagement
./StudentManagement
```

### Windows — easiest way

1. Extract **all** files from the ZIP into one folder.
2. Double-click **`START_WINDOWS.bat`**.
3. The program opens in a console window. Use the menu by typing numbers and pressing Enter.

You can also double-click `StudentManagement.exe` directly, but the batch file keeps the window open after the program exits so you can read any final message.

### Windows — build from source (MinGW-w64 / GCC)

```bat
mingw32-make run
```

Or:

```bat
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic main.c -o StudentManagement.exe
StudentManagement.exe
```

The included `StudentManagement.exe` is a 64-bit Windows build of the upgraded project. If Windows SmartScreen warns about an unrecognized app, choose **More info → Run anyway** only if you trust the source of the file.

No external C libraries, database server, or internet connection are required. Python 3 is only needed to run the automated tests.

## First run and your existing data

With the original file:

```text
1 Prabhudev 90.00
2 Prathish 85.00
3 Nivyaseelan 80.00
```

the first normal run creates:

```csv
roll,name,class,marks,attended,total
1,Prabhudev,Unassigned,90.00,0,0
2,Prathish,Unassigned,85.00,0,0
3,Nivyaseelan,Unassigned,80.00,0,0
```

- `students.txt` is **not deleted or modified**.
- Migrated students keep their roll numbers, names, and marks.
- Class defaults to `Unassigned`; update it from menu option 4.
- `attended=0,total=0` means **attendance not recorded**, not 0% attendance.
- Migration happens only when `students.csv` does not exist. An existing empty CSV is respected, so deleted records are not repeatedly re-imported.
- Run the program from the folder containing your data files.

## Menu guide

| Option | Action |
|---|---|
| 1 | Add student; roll number, grade, and result are automatic |
| 2 | View all students sorted by roll, name, marks, or class |
| 3 | Search by exact roll or partial name/class; roll search prints a report card |
| 4 | Update name, class, marks, attendance totals, or all fields |
| 5 | Delete a student after explicit confirmation |
| 6 | Record individual/whole-class attendance, correct totals, or list attendance below 75% |
| 7 | Overall summary, class summaries, rank list, report card, or students needing attention |
| 8 | Export a spreadsheet-friendly CSV report |
| 9 | Import and merge a storage CSV or exported report CSV |
| 10 | Create a manual backup |
| 11 | Validate and restore a backup after confirmation |
| 0 | Exit |

### Example: add a student

```text
Enter your choice: 1
Automatic roll number: 4
Full name: Asha Devi
Class / section (blank = General): 10 A
Marks out of 100: 95.5
Record attendance now? (y/N): y
Classes attended: 19
Total classes held: 20
Student added and saved automatically. Roll: 4; grade: A+; result: PASS.
```

### Whole-class attendance

Choose **6 → 2**, enter the class, then enter absent roll numbers separated by commas. Everyone else in that class is marked present for one session.

- Blank or `none`: all present
- `all`: all absent
- Example: `2,5,9`

The entire class session is validated before saving. Unknown rolls, rolls from another class, duplicates, or malformed lists cancel the whole session.

## Automatic calculations

### Grades and result

| Marks | Grade |
|---:|:---:|
| 90–100 | A+ |
| 80–<90 | A |
| 70–<80 | B |
| 60–<70 | C |
| 50–<60 | D |
| 40–<50 | E |
| 0–<40 | F |

Pass mark: **40**. Marks are stored and displayed to two decimal places. Equal marks share the same rank; the next rank skips tied positions (for example, 1, 1, 3).

### Attendance

```text
attendance percentage = classes attended / total classes held × 100
```

- Below **75%** is flagged.
- `total=0` is shown as `Not recorded` and excluded from attendance averages and low-attendance flags.
- Overall/class attendance is weighted by sessions: total attended divided by total held across students with recorded attendance.

## CSV formats

### Storage database (`students.csv`)

```csv
roll,name,class,marks,attended,total
1,Asha Devi,10 A,95.50,19,20
```

### Exported report (`student_report.csv`)

```csv
roll,name,class,marks,grade,result,attended,total,attendance_percent
1,Asha Devi,10 A,95.50,A+,PASS,19,20,95.00
```

Both formats can be imported. Calculated report columns are **recomputed**, never trusted from the file. Names/classes containing commas or quotes are CSV-quoted. UTF-8 BOM and Windows CRLF line endings are accepted on import.

### Import rules

- The whole file is validated before any live record changes.
- Existing roll numbers are **skipped, not overwritten**.
- Explicit incoming roll numbers are reserved first; blank roll fields are then appended with the next automatic roll numbers.
- Duplicate explicit rolls inside the incoming file reject the import.
- Invalid marks, attendance, names, classes, headers, or overlong lines reject the import.
- The merged database is saved automatically and the previous version is backed up.

## Backups and recovery

- Every successful save first preserves the previous live file as `students.csv.bak`.
- Only the **most recent** previous version is kept automatically. Use option 10 for additional named backups.
- Option 11 validates the entire restore file and asks before replacing all current records. The current version is backed up before restore.
- The live database, automatic backup, and original legacy file are protected from accidental export/backup overwrites.
- Keep external copies of important backups. Automatic backup is a recovery convenience, not a substitute for a broader backup policy.

## Command-line options

```sh
./StudentManagement --help
./StudentManagement --summary
./StudentManagement --data class10a.csv
./StudentManagement --data class10a.csv --summary
```

- `--summary` prints automatic class statistics and exits without the menu.
- `--data FILE` uses a separate CSV database and **does not** auto-import `students.txt`.
- With no options, the interactive manager uses `students.csv` and performs first-run legacy migration when needed.

## Limits

| Item | Limit |
|---|---:|
| Students | 10,000 |
| Name | 79 bytes |
| Class/section | 31 bytes |
| Marks | 0–100 |
| Attendance sessions per student | 0–10,000 |
| Roll number | 1–2,147,483,647 |
| CSV line | 2,047 bytes |

Names and classes are single-line text without control characters. Byte limits matter for multibyte UTF-8 characters.

## Testing

```sh
make test
```

The test suite uses only the Python standard library and runs the real executable in isolated temporary folders. It covers migration, CRUD, automatic calculations, attendance, CSV round trips, imports, backups, restore, corruption handling, failed-save rollback, input validation, and CLI behavior.

To test a sanitizer build on Linux/macOS:

```sh
mkdir -p build
cc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer main.c -o build/StudentManagement-sanitized
STUDENT_BINARY=build/StudentManagement-sanitized python3 -m unittest discover -s tests -v
```

## Project structure

```text
.
├── main.c                  # Complete C application
├── students.txt            # Original legacy records (preserved)
├── StudentManagement.exe   # Ready-to-run 64-bit Windows build
├── START_WINDOWS.bat       # Double-click launcher for Windows
├── Makefile                # Build, run, test, clean
├── tests/
│   └── test_system.py      # Black-box automated tests
├── .gitignore              # Ignores local builds and runtime CSV data
└── README.md
```

Runtime files such as `students.csv`, backups, and reports are intentionally not committed by the default ignore rules.
