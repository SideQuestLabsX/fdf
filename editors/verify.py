#!/usr/bin/env python3
"""Verify editor package contents. Just contents, NOT if any specific IDE accepts them"""

import json
import plistlib
import sys
import zipfile
from pathlib import Path

REQUIRED_KEYS = {"name", "scopeName", "patterns", "repository"}


def Fail(message):
    print(f"FAIL {message}")
    sys.exit(1)


def CheckGrammar(grammar, source):
    missing = REQUIRED_KEYS - grammar.keys()
    if missing:
        Fail(f"{source} is missing {sorted(missing)}")
    if grammar["scopeName"] != "source.fdf":
        Fail(f"{source} has scopeName {grammar['scopeName']}")
    if not grammar["patterns"]:
        Fail(f"{source} has no patterns")


def CheckMember(archive, name, path):
    if name not in archive.namelist():
        Fail(f"{path.name} has no {name}, it holds {archive.namelist()}")
    return archive.read(name)


def CheckPreferences(raw, source):
    preferences = plistlib.loads(raw)
    if preferences.get("scope") != "source.fdf":
        Fail(f"{source} preferences target {preferences.get('scope')}")
    if not preferences.get("settings"):
        Fail(f"{source} preferences carry no settings")


def CheckSublime(path):
    with zipfile.ZipFile(path) as archive:
        CheckGrammar(plistlib.loads(CheckMember(archive, "fdf.tmLanguage", path)), path.name)
        CheckPreferences(CheckMember(archive, "Comments.tmPreferences", path), path.name)
        CheckPreferences(CheckMember(archive, "Indentation Rules.tmPreferences", path), path.name)


def CheckTmBundle(path):
    with zipfile.ZipFile(path) as archive:
        CheckMember(archive, "fdf.tmbundle/info.plist", path)
        raw = CheckMember(archive, "fdf.tmbundle/Syntaxes/fdf.tmLanguage", path)
        CheckGrammar(plistlib.loads(raw), path.name)
        CheckPreferences(CheckMember(archive, "fdf.tmbundle/Preferences/Comments.tmPreferences", path), path.name)
        CheckPreferences(CheckMember(archive, "fdf.tmbundle/Preferences/Indentation Rules.tmPreferences", path), path.name)


def CheckVsix(path, version):
    with zipfile.ZipFile(path) as archive:
        CheckMember(archive, "extension.vsixmanifest", path)
        CheckMember(archive, "[Content_Types].xml", path)
        CheckGrammar(json.loads(CheckMember(archive, "extension/fdf.tmLanguage.json", path)), path.name)

        manifest = json.loads(CheckMember(archive, "extension/package.json", path))
        if manifest["version"] != version:
            Fail(f"{path.name} declares version {manifest['version']}, expected {version}")

        CheckMember(archive, "extension/language-configuration.json", path)
        if not manifest["contributes"]["grammars"]:
            Fail(f"{path.name} contributes no grammar")


def Main():
    if len(sys.argv) != 3:
        sys.exit("usage: verify.py <asset-dir> <version>")

    assetDir, version = Path(sys.argv[1]), sys.argv[2]
    checks = {
        "fdf.sublime-package": CheckSublime,
        "fdf.tmbundle.zip": CheckTmBundle,
        "fdf.tmLanguage.json": lambda p: CheckGrammar(json.loads(p.read_text(encoding="utf-8")), p.name),
        f"fdf-{version}.vsix": lambda p: CheckVsix(p, version),
    }

    for name, check in checks.items():
        path = assetDir / name
        if not path.exists():
            Fail(f"{name} was not produced")
        check(path)
        print(f"ok {name}")


if __name__ == "__main__":
    Main()
