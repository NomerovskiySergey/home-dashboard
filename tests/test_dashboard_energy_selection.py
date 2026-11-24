"""Host tests for energy catalogs, selection and transactional persistence."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_dashboard_recovery_source import ROOT, SDK, UI_FIXTURE


class EnergySelection(unittest.TestCase):
    def test_energy_ui_callbacks(self):
        screen = (ROOT / "main/dashboard_screen.c").read_text(encoding="utf-8")
        self.assertIn("/* Energy selection wizard", screen)
        snippet = screen[screen.index("/* Energy selection wizard"):screen.index("static void add_energy_settings_screen(")]
        with tempfile.TemporaryDirectory(prefix="energy-ui-", dir=ROOT / "build") as directory:
            path = Path(directory)
            (path / "sdk_stubs.h").write_text(SDK, encoding="utf-8")
            for header in ["esp_err.h", "nvs.h"]:
                (path / header).write_text('#include "sdk_stubs.h"\n')
            fixture = (ROOT / "tests/dashboard_energy_ui_fixture.c").read_text(encoding="utf-8")
            declarations, main = fixture.split("/* INSERT WIZARD */")
            (path / "ui.c").write_text(UI_FIXTURE + declarations + snippet + main, encoding="utf-8")
            exe = path / "ui.exe"
            result = subprocess.run(["cl", "/nologo", "/std:c11", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
                                     "/I" + str(path), "/I" + str(ROOT / "main"), str(path / "ui.c"),
                                     "/Fe:" + str(exe)], cwd=path, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            run = subprocess.run([str(exe)], cwd=path, capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout.strip())

    def test_catalogs_and_persistence(self):
        with tempfile.TemporaryDirectory(prefix="energy-selection-", dir=ROOT / "build") as directory:
            path = Path(directory)
            (path / "sdk_stubs.h").write_text(SDK, encoding="utf-8")
            for header in ["esp_err.h", "nvs.h"]:
                (path / header).write_text('#include "sdk_stubs.h"\n')
            cjson = ROOT / "managed_components/espressif__cjson/cJSON"
            exe = path / "energy.exe"
            sources = [ROOT / "tests/dashboard_energy_selection_test.c",
                       ROOT / "main/dashboard_energy.c", ROOT / "main/dashboard_energy_parse.c", cjson / "cJSON.c"]
            result = subprocess.run(["cl", "/nologo", "/std:c11", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
                                     "/I" + str(path), "/I" + str(ROOT / "main"), "/I" + str(cjson),
                                     *map(str, sources), "/Fe:" + str(exe)], cwd=path, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            run = subprocess.run([str(exe)], cwd=path, capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout.strip())


if __name__ == "__main__":
    unittest.main()
