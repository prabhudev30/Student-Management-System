"""Compile and exercise the shared record engine's transactional API."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class CoreOperationTests(unittest.TestCase):
    def test_crud_attendance_csv_backup_restore_and_legacy_migration(self):
        with tempfile.TemporaryDirectory(prefix="sms-core-") as temporary:
            binary = pathlib.Path(temporary) / "core-tests"
            compile_result = subprocess.run(
                ["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                 "-Wconversion", "-Wshadow", "-Werror", str(ROOT / "tests/test_core.c"),
                 str(ROOT / "src/student_core.c"), "-o", str(binary)],
                cwd=ROOT, text=True, capture_output=True, check=False,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            result = subprocess.run([str(binary), temporary], cwd=ROOT, text=True,
                                    capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertIn("Core data-operation tests passed.", result.stdout)


if __name__ == "__main__":
    unittest.main()
