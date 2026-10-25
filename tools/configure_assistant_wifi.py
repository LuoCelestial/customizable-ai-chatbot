#!/usr/bin/env python3
"""Prepare a private factory Wi-Fi preset; never flash or print credentials."""
from __future__ import annotations

import argparse
import getpass
import json
import os
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DESTINATION = ROOT / ".local" / "assistant-wifi.json"


def validate(value: object) -> dict[str, str]:
    if not isinstance(value, dict) or set(value) != {"ssid", "password"}:
        raise ValueError("Expected only ssid and password fields")
    ssid, password = value["ssid"], value["password"]
    if not isinstance(ssid, str) or not isinstance(password, str):
        raise ValueError("Wi-Fi fields must be text")
    if "\0" in ssid or "\0" in password:
        raise ValueError("Wi-Fi fields cannot contain NUL")
    try:
        ssid_size, password_size = len(ssid.encode("utf-8")), len(password.encode("utf-8"))
    except UnicodeError as error:
        raise ValueError("Wi-Fi fields require valid UTF-8") from error
    if not 1 <= ssid_size <= 32:
        raise ValueError("SSID requires 1 to 32 UTF-8 bytes")
    if password_size and not 8 <= password_size <= 63:
        raise ValueError("Password requires 8 to 63 UTF-8 bytes; empty means open network")
    return {"ssid": ssid, "password": password}


def write_preset(value: object, destination: Path = DESTINATION) -> None:
    data = validate(value)
    # Do not follow a replaced private directory or overwrite through a symlink.
    if destination.parent.is_symlink() or destination.is_symlink():
        raise ValueError("Preset directory and file must not be symlinks")
    destination.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    temporary = None
    try:
        descriptor, temporary = tempfile.mkstemp(prefix=".wifi-", dir=destination.parent)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(data, stream, ensure_ascii=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
        temporary = None
    finally:
        if temporary is not None:
            os.unlink(temporary)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--input", type=Path, help="Read a local JSON file; credentials are never echoed")
    mode.add_argument("--check", action="store_true", help="Read-only validation of the existing private preset")
    args = parser.parse_args(argv)
    try:
        if args.check:
            validate(json.loads(DESTINATION.read_text(encoding="utf-8")))
            print("Private Wi-Fi preset: valid")
            return 0
        if args.input:
            value = json.loads(args.input.read_text(encoding="utf-8"))
        else:
            value = {"ssid": input("2.4 GHz Wi-Fi SSID: "), "password": getpass.getpass("Wi-Fi password (hidden): ")}
        write_preset(value)
    except (OSError, ValueError, EOFError):
        print("Wi-Fi preset was not saved; check fields, input file and permissions. Existing preset was preserved.", file=sys.stderr)
        return 1
    print("Private Wi-Fi preset saved under .local/ (ignored by Git).")
    print("Rebuild with ./tools/validate.sh. The resulting firmware contains credentials; keep it private.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
