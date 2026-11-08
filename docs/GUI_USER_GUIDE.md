# Student Management Studio — GUI quick guide

## Launch and storage

On Windows, extract the full project/Windows ZIP and double-click `START_GUI_WINDOWS.bat`. The graphical app is `StudentManagementGUI.exe`; it is separate from the retained console executable. No internet connection is used.

The live database, automatic save backup, and migration target are stored in `%APPDATA%\StudentManagementSystem`. The app checks the executable's folder for an existing `students.csv` or legacy `students.txt` only when no user-data CSV exists. Existing sources are copied/migrated without being deleted or replaced. The source files are protected from CSV export and manual backup overwrite.

## Main areas

- **Dashboard:** total students, class average, pass/fail totals, highest marks, and low-attendance count.
- **Students:** type in the search field to filter by name, class, or roll. Click table headings to sort. `Rank` uses competition ranking (ties share a rank and subsequent rank positions are skipped).
- **Rankings:** the same table ordered by marks from highest to lowest.
- **Academic report:** use the Report button for class totals and summary statistics.

## Records and attendance

- **Add student:** enter a unique profile; roll number is assigned automatically. Marks determine grade and pass/fail result.
- **Edit:** select a row and update the student's name, class, or marks.
- **View:** select a row to see the full profile and calculated fields.
- **Delete:** select a row and confirm the deletion.
- **Attendance:** select a row, choose Present or Absent, then save one new class session for that student. Attendance changes save immediately.

## Files and recovery

- **Import CSV:** merges compatible storage or report CSV files. Existing roll numbers are skipped, and calculated columns are recomputed.
- **Export CSV:** writes a detailed report with grade, result, attendance percentage, and rank-compatible marks. The live database, backup, and migration sources are protected.
- **Backup:** creates a manual validated copy at a location you choose.
- **Restore:** validates the selected backup and asks for confirmation. The current live database is backed up before restore.
- Every successful change is automatically saved; the previous version is retained as the automatic backup.

For the console workflow, use `START_WINDOWS.bat`. For source build instructions and tests, see the repository `README.md`.
