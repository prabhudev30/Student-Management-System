"""Source-level regression checks for the Win32 GUI (not a Windows runtime test)."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
GUI = (ROOT / "gui/win32_main.c").read_text(encoding="ascii")
MANIFEST = (ROOT / "gui/app.manifest").read_text(encoding="ascii")


class GuiSourceRegressionTests(unittest.TestCase):
    def test_layout_dpi_typography_and_encoding_hooks_remain_present(self):
        for marker in (
            "BS_OWNERDRAW", "draw_stat_card", "WM_DPICHANGED", "WM_GETMINMAXINFO",
            "button_w=(content-4*gap)/5", "ID_BRAND_DETAIL", "CreateFontA", "Segoe UI",
            "MultiByteToWideChar(CP_UTF8", "WideCharToMultiByte(CP_UTF8", "LVS_REPORT",
            "sms_mark_attendance(&db,roll,d.present)", "ID_FORM_STATUS",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, GUI)
        self.assertIn("PerMonitorV2", MANIFEST)
        self.assertTrue(GUI.isascii(), "GUI source text should stay ASCII-safe for ANSI labels")

    def test_migration_is_additive_and_source_marked(self):
        self.assertIn("sms_import_csv(&db,path", GUI)
        self.assertIn("sms_import_legacy(&db,path", GUI)
        self.assertIn("CREATE_NEW", GUI)
        self.assertIn("migration-local-csv.done", GUI)
        self.assertIn("Existing students will not be changed", GUI)
        self.assertIn("copy_csv_without_replace", GUI)
        self.assertIn("migration-external-source.path", GUI)
        self.assertIn("offer_external_csv_once", GUI)


if __name__ == "__main__":
    unittest.main()
