#!/usr/bin/env python3

"""Verify that the shared library exposes only the stable C ABI."""

from __future__ import annotations

import subprocess
import sys


EXPECTED_EXPORTS = {
    "dvdcss_close",
    "dvdcss_error",
    "dvdcss_get_encryption_type",
    "dvdcss_is_scrambled",
    "dvdcss_open",
    "dvdcss_open_stream",
    "dvdcss_read",
    "dvdcss_readv",
    "dvdcss_seek",
    "dvdcpxm_close",
    "dvdcpxm_init",
    "dvdcpxm_read",
    "dvdcpxm_readv",
    "dvdcpxm_seek",
}


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} NM LIBRARY", file=sys.stderr)
        return 2

    result = subprocess.run(
        [sys.argv[1], "-D", "--defined-only", sys.argv[2]],
        check=True,
        capture_output=True,
        text=True,
    )
    actual = {
        line.split()[-1].split("@", maxsplit=1)[0]
        for line in result.stdout.splitlines()
        if line.split()
    }

    if actual != EXPECTED_EXPORTS:
        missing = sorted(EXPECTED_EXPORTS - actual)
        unexpected = sorted(actual - EXPECTED_EXPORTS)
        if missing:
            print(f"missing exports: {', '.join(missing)}", file=sys.stderr)
        if unexpected:
            print(f"unexpected exports: {', '.join(unexpected)}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
