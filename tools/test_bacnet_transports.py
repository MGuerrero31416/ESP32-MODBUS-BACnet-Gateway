"""Exercise real BACnet services/TSM/datalink dispatch with captured wire boundaries."""

import argparse
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", nargs="+", default=["gcc"])
    args = parser.parse_args()
    stack = ROOT / "components" / "bacnet-stack" / "src"
    sources = list((stack / "bacnet").glob("*.c"))
    sources += [
        stack / "bacnet" / "basic" / "service" / f"{name}.c"
        for name in ("h_apdu", "h_cov", "h_whois", "h_rp", "s_iam", "s_whoami")
    ]
    sources += [
        stack / "bacnet" / "basic" / "sys" / f"{name}.c"
        for name in ("days", "debug")
    ]
    sources += [
        stack / "bacnet" / "basic" / "tsm" / "tsm.c",
        stack / "bacnet" / "datalink" / "datalink.c",
    ]
    with tempfile.TemporaryDirectory() as directory:
        executable = Path(directory) / "transports.exe"
        subprocess.run(
            args.compiler + [
                "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-Wno-sign-compare", "-Wno-deprecated-declarations",
                "-ffunction-sections", "-fdata-sections",
                "-DBACDL_MULTIPLE=1", "-DBACDL_BIP=1", "-DBACDL_MSTP=1",
                "-I", str(stack),
                str(ROOT / "tools" / "test_bacnet_transports.c"),
                *map(str, sources), "-Wl,--gc-sections", "-lm",
                "-o", str(executable),
            ],
            check=True,
        )
        for mode in ("bip", "mstp", "dual"):
            subprocess.run([str(executable), mode], check=True)


if __name__ == "__main__":
    main()
