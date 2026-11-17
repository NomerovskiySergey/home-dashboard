"""Write a service API key to the dashboard’s USB-connected ESP32."""

import argparse
import csv
import getpass
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import warnings


DEFAULT_INSTALL = Path("C:/Espressif")
PARTITION_SIZE = "0x6000"
SERVICES = {
    "alerts": ("dash_keys", "0x410000", "alerts_token", "Alerts.in.ua"),
    "energy": ("energy_keys", "0x416000", "energy_key", "Alerts.energy"),
}


def sdk_tools(parser):
    default_idf = DEFAULT_INSTALL / "v6.1/esp-idf"
    idf = Path(os.environ.get("IDF_PATH") or default_idf)
    generator = idf / "components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
    parttool = idf / "components/partition_table/parttool.py"
    if not generator.is_file() or not parttool.is_file():
        parser.error("ESP-IDF tools are missing. Set IDF_PATH or use the installed ESP-IDF 6.1 terminal.")

    active_python = os.environ.get("IDF_PYTHON_ENV_PATH")
    if active_python:
        python = Path(active_python) / "Scripts/python.exe"
    elif idf == default_idf:
        python = DEFAULT_INSTALL / "tools/python/v6.1/venv/Scripts/python.exe"
    else:
        python = Path(sys.executable)
    if not python.is_file():
        parser.error("ESP-IDF Python was not found. Open the ESP-IDF 6.1 terminal and try again.")
    return python, generator, parttool


def read_token(parser, service="Alerts.in.ua"):
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("error", getpass.GetPassWarning)
            return getpass.getpass(f"{service} API key: ")
    except (getpass.GetPassWarning, EOFError, OSError):
        parser.error("Hidden input is unavailable. Run the command directly in a PowerShell window.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="ESP32 serial port, for example COM3")
    parser.add_argument("--service", choices=SERVICES, default="alerts")
    args = parser.parse_args()
    partition_name, offset, nvs_key, service = SERVICES[args.service]
    python, generator, parttool = sdk_tools(parser)
    partition = [str(python), str(parttool), "--port", args.port]
    selector = ["--partition-name", partition_name]

    try:
        output = subprocess.check_output(
            [*partition, "get_partition_info", *selector, "--info", "offset", "size"],
            text=True,
        )
    except subprocess.CalledProcessError:
        parser.error(f"Could not read {partition_name} via {args.port}. Check the USB port and close the serial monitor.")
    if output.strip().splitlines()[-1:] != [f"{offset} {PARTITION_SIZE}"]:
        parser.error(f"Expected {partition_name} at {offset} with size {PARTITION_SIZE}. Flash the dashboard first.")

    token = read_token(parser, service)
    if not re.fullmatch(r"[!-~]{1,256}", token):
        parser.error("Enter the token as a single line without spaces.")

    with tempfile.TemporaryDirectory(prefix="dashboard-key-") as directory:
        source = Path(directory) / "credentials.csv"
        binary = Path(directory) / f"{partition_name}.bin"
        with source.open("w", newline="", encoding="utf-8") as output:
            table = csv.writer(output)
            table.writerow(["key", "type", "encoding", "value"])
            table.writerow(["credentials", "namespace", "", ""])
            table.writerow([nvs_key, "data", "string", token])
        del token
        subprocess.run([str(python), str(generator), "generate", str(source),
                        str(binary), PARTITION_SIZE], check=True)
        subprocess.run([*partition, "write_partition", *selector, "--input",
                        str(binary)], check=True)
    print(f"{service} key saved to the device. Restart ESP32 to load it.")


if __name__ == "__main__":
    main()
