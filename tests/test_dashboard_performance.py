"""Exercise real UI refresh functions with allocation/write counters (MSVC)."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_dashboard_recovery_source import ROOT, SDK


class Performance(unittest.TestCase):
    def test_ui_refresh_does_not_rebuild_unchanged_content(self):
        screen = (ROOT / "main/dashboard_screen.c").read_text(encoding="utf-8")
        spans = [
            ("static bool has_plan(", "static time_t kyiv_clock("),
            ("static const dashboard_energy_day_t *day_for_date(", "static void change_schedule_day("),
            ("static void update_energy_cards(", "static void request_setup("),
            ("static void set_label_if_changed(lv_obj_t *object, const char *text)", "static void update_wifi_connection(lv_timer_t *timer)\n{"),
            ("static void update_age(", "static void update_weather_card("),
            ("static void update_alert_card(const dashboard_data_snapshot_t *data)\n{", "/* Badges represent territorial threats"),
        ]
        snippets = "\n".join(screen[screen.index(a):screen.index(b, screen.index(a))] for a, b in spans)
        fixture = (ROOT / "tests/dashboard_performance_ui_fixture.c").read_text(encoding="utf-8")
        declarations, main = fixture.split("/* INSERT UI */")
        with tempfile.TemporaryDirectory(prefix="performance-ui-", dir=ROOT / "build") as directory:
            path = Path(directory)
            (path / "sdk_stubs.h").write_text(SDK, encoding="utf-8")
            (path / "esp_err.h").write_text('#include "sdk_stubs.h"\n')
            (path / "ui.c").write_text(declarations + snippets + main, encoding="utf-8")
            cjson = ROOT / "managed_components/espressif__cjson/cJSON"
            exe = path / "performance.exe"
            result = subprocess.run([
                "cl", "/nologo", "/std:c11", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
                "/I" + str(path), "/I" + str(ROOT / "main"), "/I" + str(cjson),
                str(path / "ui.c"), str(ROOT / "main/dashboard_alert_details.c"),
                str(cjson / "cJSON.c"), "/Fe:" + str(exe),
            ], cwd=path, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for mode in ["alert", "schedule"]:
                with self.subTest(mode=mode):
                    run = subprocess.run([str(exe), mode], cwd=path, capture_output=True, text=True, timeout=10)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    print(run.stdout.strip())


if __name__ == "__main__":
    unittest.main()
