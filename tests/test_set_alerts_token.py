import getpass
import io
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import warnings


SCRIPT = Path(__file__).resolve().parents[1] / "main/tools/set_alerts_token.py"


class SetAlertsTokenTest(unittest.TestCase):
    def setUp(self):
        self.main = runpy.run_path(str(SCRIPT))["main"]

    def check_service(self, options, device_info, partition):
        with tempfile.TemporaryDirectory(dir=SCRIPT.parents[2]) as directory:
            install = Path(directory)
            sdk = install / "v6.1/esp-idf"
            python = install / "tools/python/v6.1/venv/Scripts/python.exe"
            for file in (
                python,
                sdk / "components/partition_table/parttool.py",
                sdk / "components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py",
            ):
                file.parent.mkdir(parents=True, exist_ok=True)
                file.touch()
            with (
                patch.dict(os.environ, {}, clear=True),
                patch.dict(self.main.__globals__, {"DEFAULT_INSTALL": install}),
                patch.object(sys, "argv", [str(SCRIPT), "--port", "COM99", *options]),
                patch.object(subprocess, "check_output",
                             return_value=f"esptool status\n{device_info}\n") as read,
                patch.object(subprocess, "run") as run,
                patch.object(tempfile, "tempdir", directory),
                patch.object(getpass, "getpass", return_value="fixture-key"),
                patch("sys.stdout", io.StringIO()),
            ):
                self.main()
            self.assertEqual(read.call_args.args[0][0], str(python))
            self.assertEqual(run.call_count, 2)
            self.assertIn("write_partition", run.call_args.args[0])
            self.assertIn(partition, read.call_args.args[0])
            self.assertIn(partition, run.call_args.args[0])
            self.assertNotIn("fixture-key", str(run.call_args_list))

    def test_default_service_uses_alert_partition(self):
        self.check_service([], "0x410000 0x6000", "dash_keys")

    def test_energy_uses_separate_partition(self):
        self.check_service(["--service", "energy"], "0x416000 0x6000", "energy_keys")

    def test_hidden_prompt_cannot_fall_back_to_echo(self):
        read_token = self.main.__globals__["read_token"]
        with (
            patch.object(getpass, "getpass",
                         side_effect=lambda _: warnings.warn("echo", getpass.GetPassWarning)),
            patch("sys.stderr", io.StringIO()) as stderr,
            self.assertRaises(SystemExit),
        ):
            import argparse
            read_token(argparse.ArgumentParser())
        self.assertIn("Hidden input is unavailable", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
