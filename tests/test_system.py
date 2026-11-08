"""Black-box tests for the C student manager (Python standard library only).

Run `make test`, or `python3 -m unittest discover -s tests -v` after building.
Set STUDENT_BINARY to test a different build (for example a sanitizer build).
Every test uses a temporary working directory and never changes project data.
"""
import csv
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BINARY = ROOT / ("StudentManagement.exe" if os.name == "nt" else "StudentManagement")
BINARY = Path(os.environ.get("STUDENT_BINARY", str(DEFAULT_BINARY))).resolve()
HEADER = ["roll", "name", "class", "marks", "attended", "total"]
REPORT_HEADER = ["roll", "name", "class", "marks", "grade", "result", "attended", "total", "attendance_percent"]


class StudentSystemTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BINARY.is_file():
            raise unittest.SkipTest(f"Build the C program first: {BINARY} (or set STUDENT_BINARY)")

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="student-management-test-")
        self.work = Path(self.temp.name)
        self.data = self.work / "students.csv"

    def tearDown(self):
        self.temp.cleanup()

    def run_app(self, keys="", *args, expected=0):
        result = subprocess.run(
            [str(BINARY), *args], input=keys, text=True, capture_output=True,
            cwd=self.work, timeout=15,
        )
        self.assertEqual(result.returncode, expected, msg=result.stdout + "\n" + result.stderr)
        return result

    def interactive(self, keys="", expected=0):
        return self.run_app(keys + "0\n", "--data", "students.csv", expected=expected)

    def seed(self, rows=None, header=HEADER):
        if rows is None:
            rows = [
                [1, "Prabhudev", "10 A", "90.00", 0, 0],
                [2, "Prathish", "10 A", "85.00", 0, 0],
                [3, "Nivyaseelan", "10 B", "80.00", 0, 0],
            ]
        with self.data.open("w", newline="", encoding="utf-8") as file:
            writer = csv.writer(file, lineterminator="\n")
            writer.writerow(header)
            writer.writerows(rows)

    def records(self, path=None):
        with (path or self.data).open(newline="", encoding="utf-8-sig") as file:
            return list(csv.DictReader(file))

    def record(self, roll, path=None):
        return next(row for row in self.records(path) if row["roll"] == str(roll))

    def test_empty_database_and_eof_exit(self):
        result = self.run_app("", "--data", "students.csv")
        self.assertIn("Created a new empty database", result.stdout)
        self.assertIn("Input ended. Exiting safely", result.stdout)
        self.assertEqual(self.records(), [])

    def test_legacy_migration_preserves_original_and_all_records(self):
        original = "1 Prabhudev 90.00\n2 Prathish 85.00\n3 Nivyaseelan 80.00\n"
        legacy = self.work / "students.txt"
        legacy.write_text(original, encoding="utf-8")
        result = self.run_app("0\n")
        self.assertIn("Automatically migrated 3 record(s)", result.stdout)
        self.assertEqual(legacy.read_text(encoding="utf-8"), original)
        rows = self.records()
        self.assertEqual([row["name"] for row in rows], ["Prabhudev", "Prathish", "Nivyaseelan"])
        self.assertEqual([row["marks"] for row in rows], ["90.00", "85.00", "80.00"])
        self.assertTrue(all(row["class"] == "Unassigned" and row["total"] == "0" for row in rows))
        again = self.run_app("0\n")
        self.assertNotIn("migrated", again.stdout)
        self.assertIn("3 student record(s) loaded", again.stdout)

    def test_legacy_full_name_and_tabs(self):
        (self.work / "students.txt").write_text("7\tAsha Devi\t91.25\n", encoding="utf-8")
        self.run_app("0\n")
        self.assertEqual(self.record(7)["name"], "Asha Devi")
        self.assertEqual(self.record(7)["marks"], "91.25")

    def test_custom_data_does_not_import_legacy(self):
        (self.work / "students.txt").write_text("1 Existing 90\n", encoding="utf-8")
        self.interactive()
        self.assertEqual(self.records(), [])

    def test_add_automatic_roll_grade_attendance_and_persistence(self):
        self.seed()
        result = self.interactive("1\nAsha Devi\n10 A\n95.5\ny\n19\n20\n")
        self.assertIn("Automatic roll number: 4", result.stdout)
        self.assertIn("grade: A+; result: PASS", result.stdout)
        row = self.record(4)
        self.assertEqual(row["name"], "Asha Devi")
        self.assertEqual(row["marks"], "95.50")
        self.assertEqual((row["attended"], row["total"]), ("19", "20"))
        self.assertTrue((self.work / "students.csv.bak").is_file())
        self.assertEqual(len(self.records(self.work / "students.csv.bak")), 3)
        reload = self.interactive()
        self.assertIn("4 student record(s) loaded", reload.stdout)

    def test_next_roll_uses_maximum_not_record_count(self):
        self.seed([[10, "Existing", "A", "50.00", 0, 0]])
        self.interactive("1\nNew Student\nB\n60\nn\n5\n10\nyes\n1\nAnother Student\nB\n70\nn\n")
        self.assertEqual([row["roll"] for row in self.records()], ["11", "12"])

    def test_invalid_add_input_reprompts_and_eof_does_not_save_partial(self):
        self.seed([])
        result = self.interactive("1\nNew Student\nA\nabc\n101\n-1\nnan\ninf\n80\ny\n5\n3\n")
        self.assertIn("Enter a number from 0.00 to 100.00", result.stdout)
        self.assertIn("Classes attended cannot exceed classes held", result.stdout)
        self.assertEqual(self.records(), [])

    def test_update_blank_keeps_values_and_recalculates_grade(self):
        self.seed()
        result = self.interactive("4\n1\n5\n\n\n39.99\n2\n3\n")
        self.assertIn("Grade:       F", result.stdout)
        row = self.record(1)
        self.assertEqual(row["name"], "Prabhudev")
        self.assertEqual(row["class"], "10 A")
        self.assertEqual(row["marks"], "39.99")
        self.assertEqual((row["attended"], row["total"]), ("2", "3"))
        self.assertEqual(self.record(1, self.work / "students.csv.bak")["marks"], "90.00")

    def test_search_roll_name_class_and_missing(self):
        self.seed()
        result = self.interactive("3\n1\n2\n3\n2\nasha\n3\n2\nPRATHI\n3\n3\n10 b\n3\n1\n999\n")
        self.assertIn("Student report card", result.stdout)
        self.assertIn("No matching student.", result.stdout)
        self.assertIn("No matching students.", result.stdout)
        self.assertIn("1 matching student(s).", result.stdout)
        self.assertIn("Nivyaseelan", result.stdout)

    def test_delete_requires_confirmation_and_backs_up(self):
        self.seed()
        self.interactive("5\n2\nn\n")
        self.assertEqual(len(self.records()), 3)
        result = self.interactive("5\n2\nyes\n")
        self.assertIn("Student deleted and saved automatically", result.stdout)
        self.assertEqual([row["roll"] for row in self.records()], ["1", "3"])
        self.assertEqual(len(self.records(self.work / "students.csv.bak")), 3)

    def test_sorting_does_not_change_stored_order(self):
        self.seed()
        before = self.records()
        result = self.interactive("2\n3\n2\n2\n")
        rank_section = result.stdout.split("Roll    Name")[1]
        self.assertLess(rank_section.index("Prabhudev"), rank_section.index("Nivyaseelan"))
        self.assertEqual(self.records(), before)

    def test_individual_attendance_and_limit(self):
        self.seed([[1, "Student", "A", "80.00", 0, 0]])
        result = self.interactive("6\n1\n1\nP\n6\n1\n1\nA\n6\n1\n1\nX\n")
        self.assertIn("50.00% (1/2)", result.stdout)
        self.assertIn("Invalid status", result.stdout)
        self.assertEqual((self.record(1)["attended"], self.record(1)["total"]), ("1", "2"))
        self.seed([[1, "Student", "A", "80.00", 10000, 10000]])
        limit = self.interactive("6\n1\n1\nP\n")
        self.assertIn("session limit has been reached", limit.stdout)
        self.assertEqual(self.record(1)["total"], "10000")

    def test_whole_class_attendance_is_atomic_and_recalculates(self):
        self.seed()
        result = self.interactive("6\n2\n10 a\n2\n6\n2\n10 A\nall\n")
        self.assertIn("1 present, 1 absent, 2 total", result.stdout)
        self.assertIn("0 present, 2 absent, 2 total", result.stdout)
        self.assertEqual((self.record(1)["attended"], self.record(1)["total"]), ("1", "2"))
        self.assertEqual((self.record(2)["attended"], self.record(2)["total"]), ("0", "2"))
        self.assertEqual((self.record(3)["attended"], self.record(3)["total"]), ("0", "0"))
        before = self.records()
        for absent in ("2,2", "3", "abc", "2,"):
            bad = self.interactive(f"6\n2\n10 A\n{absent}\n")
            self.assertIn("No attendance was changed", bad.stdout)
            self.assertEqual(self.records(), before)

    def test_attendance_correction_and_low_report_ignore_missing(self):
        self.seed([[1, "Low", "A", "80.00", 1, 4], [2, "Missing", "A", "80.00", 0, 0]])
        result = self.interactive("6\n4\n6\n3\n1\n9\n10\n")
        self.assertIn("1 student(s) below 75%", result.stdout)
        self.assertIn("1 with no attendance recorded", result.stdout)
        self.assertEqual((self.record(1)["attended"], self.record(1)["total"]), ("9", "10"))

    def test_summary_ranks_ties_and_grade_boundaries(self):
        self.seed([
            [1, "Top", "A", "90.00", 3, 4],
            [2, "Tie", "A", "90.00", 4, 4],
            [3, "Pass", "B", "40.00", 0, 0],
            [4, "Fail", "B", "39.99", 0, 0],
        ])
        result = self.interactive("7\n1\n7\n3\n7\n2\n7\n5\n")
        self.assertIn("Average marks:  65.00", result.stdout)
        self.assertIn("Passed / failed: 3 / 1", result.stdout)
        self.assertIn("Pass percentage: 75.00%", result.stdout)
        self.assertIn("Attendance:     87.50%", result.stdout)
        self.assertIn("A+=2", result.stdout)
        lines = [line for line in result.stdout.splitlines() if line.startswith(("1      ", "3      "))]
        self.assertTrue(any("Top" in line for line in lines if line.startswith("1")))
        self.assertTrue(any("Tie" in line for line in lines if line.startswith("1")))
        self.assertTrue(any("Pass" in line for line in lines if line.startswith("3")))
        self.assertIn("1 student(s) need attention", result.stdout)

    def test_export_round_trip_quotes_commas_and_recomputes_derived_columns(self):
        self.seed([[1, 'Asha "Devi", Rao', "10, A", "95.50", 19, 20], [2, "No Attendance", "B", "39.00", 0, 0]])
        self.interactive("8\nreport.csv\n")
        report = self.work / "report.csv"
        rows = self.records(report)
        self.assertEqual(list(rows[0]), REPORT_HEADER)
        self.assertEqual(rows[0]["name"], 'Asha "Devi", Rao')
        self.assertEqual(rows[0]["class"], "10, A")
        self.assertEqual((rows[0]["grade"], rows[0]["result"], rows[0]["attendance_percent"]), ("A+", "PASS", "95.00"))
        self.assertEqual((rows[1]["grade"], rows[1]["result"], rows[1]["attendance_percent"]), ("F", "FAIL", "N/A"))
        # Stale/forged calculated fields must never override marks or attendance.
        rows[0]["grade"], rows[0]["result"], rows[0]["attendance_percent"] = "F", "FAIL", "0.00"
        with report.open("w", newline="", encoding="utf-8") as file:
            writer = csv.DictWriter(file, fieldnames=REPORT_HEADER, lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
        self.data.unlink()
        result = self.interactive("9\nreport.csv\n7\n4\n1\n")
        self.assertIn("2 added, 0 duplicate", result.stdout)
        self.assertIn("Grade:       A+", result.stdout)
        self.assertIn("Attendance:  95.00% (19/20)", result.stdout)

    def test_import_merges_skips_duplicates_and_assigns_blank_rolls(self):
        self.seed([[10, "Existing", "A", "50.00", 1, 2]])
        incoming = self.work / "incoming.csv"
        with incoming.open("w", newline="", encoding="utf-8") as file:
            writer = csv.writer(file, lineterminator="\n")
            writer.writerow(HEADER)
            writer.writerows([[10, "Do not overwrite", "B", 100, 9, 9], ["", "Auto One", "C", 60, 0, 0], [12, "Preserved", "B", 70, 0, 0], ["", "Auto Two", "C", 60, 0, 0]])
        result = self.interactive("9\nincoming.csv\n")
        self.assertIn("3 added, 1 duplicate", result.stdout)
        self.assertEqual(self.record(10)["name"], "Existing")
        self.assertEqual([row["roll"] for row in self.records()], ["10", "12", "13", "14"])
        self.assertEqual(self.record(12)["name"], "Preserved")

    def test_invalid_import_never_changes_live_data_or_backup(self):
        self.seed()
        before = self.data.read_bytes()
        incoming = self.work / "bad.csv"
        incoming.write_text("roll,name,class,marks,attended,total\n4,Valid,A,80,0,0\n5,Bad,A,80,9,2\n", encoding="utf-8")
        result = self.interactive("9\nbad.csv\n")
        self.assertIn("invalid record", result.stderr)
        self.assertEqual(self.data.read_bytes(), before)
        self.assertFalse((self.work / "students.csv.bak").exists())

    def test_import_capacity_is_atomic(self):
        self.seed([[1, "Existing", "A", "80.00", 0, 0]])
        incoming = self.work / "too-many.csv"
        with incoming.open("w", newline="", encoding="utf-8") as file:
            writer = csv.writer(file)
            writer.writerow(HEADER)
            writer.writerows([[i, f"Student {i}", "A", 80, 0, 0] for i in range(2, 10002)])
        before = self.data.read_bytes()
        result = self.interactive("9\ntoo-many.csv\n")
        self.assertIn("No records were changed", result.stdout)
        self.assertEqual(self.data.read_bytes(), before)

    def test_backup_and_restore_validate_and_confirm(self):
        self.seed()
        self.interactive("10\nmanual.csv\n4\n1\n3\n20\nn\n")
        self.assertEqual(len(self.records(self.work / "manual.csv")), 3)
        cancelled = self.interactive("11\nmanual.csv\nno\n")
        self.assertIn("Restore cancelled", cancelled.stdout)
        self.assertEqual(self.record(1)["marks"], "20.00")
        restored = self.interactive("11\nmanual.csv\nyes\n")
        self.assertIn("Restored 3 record(s)", restored.stdout)
        self.assertEqual(self.record(1)["marks"], "90.00")
        self.assertEqual(self.record(1, self.work / "students.csv.bak")["marks"], "20.00")
        bad = self.work / "bad.csv"
        bad.write_text("roll,name,class,marks,attended,total\n1,Bad,A,101,0,0\n", encoding="utf-8")
        before = self.data.read_bytes()
        self.interactive("11\nbad.csv\nyes\n")
        self.assertEqual(self.data.read_bytes(), before)

    def test_default_restore_uses_automatic_backup(self):
        self.seed()
        self.interactive("4\n1\n3\n10\nn\n")
        result = self.interactive("11\n\nyes\n")
        self.assertIn("Restored 3 record(s)", result.stdout)
        self.assertEqual(self.record(1)["marks"], "90.00")

    def test_corrupt_database_stops_without_overwriting(self):
        cases = [
            "wrong,header\n",
            "roll,name,class,marks,attended,total\n1,A,A,80,0,0\n1,B,B,90,0,0\n",
            "roll,name,class,marks,attended,total\n1,A,A,nan,0,0\n",
            "roll,name,class,marks,attended,total\n1,A,A,80,3,2\n",
            'roll,name,class,marks,attended,total\n1,"unclosed,A,80,0,0\n',
            "roll,name,class,marks,attended,total\n1,A,A,80,0,0," + "x" * 2100 + "\n",
        ]
        for content in cases:
            with self.subTest(content=content[:70]):
                self.data.write_text(content, encoding="utf-8")
                before = self.data.read_bytes()
                result = self.interactive(expected=1)
                self.assertIn("Startup stopped safely", result.stderr)
                self.assertEqual(self.data.read_bytes(), before)

    def test_failed_backup_rolls_back_memory_and_disk(self):
        self.seed()
        # A directory at the backup destination forces replacement to fail.
        (self.work / "students.csv.bak").mkdir()
        before = self.data.read_bytes()
        result = self.interactive("4\n1\n3\n10\nn\n2\n1\n")
        self.assertIn("Automatic backup failed", result.stderr)
        self.assertIn("Update cancelled", result.stdout)
        self.assertEqual(self.data.read_bytes(), before)
        self.assertIn("90.00", result.stdout.split("Update cancelled")[1])

    def test_output_protection_including_relative_alias(self):
        self.seed()
        before = self.data.read_bytes()
        result = self.interactive("8\nstudents.csv\n8\n./students.csv\n10\nstudents.csv.bak\n")
        self.assertEqual(result.stdout.count("Choose a separate output file"), 3)
        self.assertEqual(self.data.read_bytes(), before)

    def test_overlong_interactive_input_is_discarded_not_executed(self):
        self.seed()
        result = self.interactive("x" * 3000 + "\n2\n1\n")
        self.assertIn("Invalid choice", result.stdout)
        self.assertIn("3 record(s).", result.stdout)
        self.assertEqual(len(self.records()), 3)

    def test_csv_bom_crlf_and_blank_lines(self):
        self.data.write_bytes(b"\xef\xbb\xbfroll,name,class,marks,attended,total\r\n1,Asha Devi,10 A,88.50,9,10\r\n\r\n")
        result = self.interactive("7\n4\n1\n")
        self.assertIn("Asha Devi", result.stdout)
        self.assertEqual(self.record(1)["marks"], "88.50")

    def test_maximum_roll_disables_automatic_assignment_only(self):
        self.seed([[2147483647, "Maximum", "A", "80.00", 0, 0]])
        result = self.interactive("1\n")
        self.assertIn("No automatic roll numbers remain", result.stdout)
        incoming = self.work / "blank.csv"
        incoming.write_text("roll,name,class,marks,attended,total\n,Blank,A,80,0,0\n", encoding="utf-8")
        result = self.interactive("9\nblank.csv\n")
        self.assertIn("No records were changed", result.stdout)
        self.assertEqual(len(self.records()), 1)

    def test_cli_help_summary_and_bad_options(self):
        help_result = self.run_app("", "--help")
        self.assertIn("--data FILE", help_result.stdout)
        self.run_app("", "--data", expected=1)
        self.run_app("", "--unknown", expected=1)
        self.seed()
        summary = self.run_app("", "--data", "students.csv", "--summary")
        self.assertIn("Average marks:  85.00", summary.stdout)
        self.assertNotIn("Enter your choice", summary.stdout)


if __name__ == "__main__":
    unittest.main()
