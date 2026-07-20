#!/usr/bin/env python3
"""Build editor packages from the canonical grammar."""

import argparse
import json
import plistlib
import shutil
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent

# fixed for reproducible archives (byte identical)
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def LoadGrammar():
    with (HERE / "fdf.tmLanguage.json").open(encoding="utf-8") as f:
        grammar = json.load(f)
    grammar.pop("$schema", None)
    return grammar


def LoadLanguageConfiguration():
    with (HERE / "language-configuration.json").open(encoding="utf-8") as f:
        return json.load(f)


def BuildPreferences(config):
    lineComment = config["comments"]["lineComment"]
    blockOpen, blockClose = config["comments"]["blockComment"]

    comments = plistlib.dumps({
        "scope": "source.fdf",
        "settings": {
            "shellVariables": [
                { "name": "TM_COMMENT_START",   "value": f"{lineComment} " },
                { "name": "TM_COMMENT_START_2", "value": f"{blockOpen} " },
                { "name": "TM_COMMENT_END_2",   "value": f" {blockClose}" },
            ]
        },
    }, sort_keys=False)

    indentation = plistlib.dumps({
        "scope": "source.fdf",
        "settings": {
            "increaseIndentPattern": config["indentationRules"]["increaseIndentPattern"],
            "decreaseIndentPattern": config["indentationRules"]["decreaseIndentPattern"],
        },
    }, sort_keys=False)

    return comments, indentation


def WritePlist(grammar, path):
    path.write_bytes(plistlib.dumps(grammar, sort_keys=False))


def AddFile(archive, name, data):
    info = zipfile.ZipInfo(name, date_time=ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    archive.writestr(info, data)


def BuildSublime(plistData, preferences, outPath):
    comments, indentation = preferences
    with zipfile.ZipFile(outPath, "w") as archive:
        AddFile(archive, "fdf.tmLanguage", plistData)
        AddFile(archive, "Comments.tmPreferences", comments)
        AddFile(archive, "Indentation Rules.tmPreferences", indentation)


def BuildTmBundle(plistData, preferences, version, outPath):
    comments, indentation = preferences
    info = plistlib.dumps({
        "name": "fdf",
        "description": f"fdf syntax highlighting {version}",
        "uuid": "5f2a1c94-3d6e-4a7b-9c8d-1e0f2a3b4c5d",
    }, sort_keys=False)

    with zipfile.ZipFile(outPath, "w") as archive:
        AddFile(archive, "fdf.tmbundle/info.plist", info)
        AddFile(archive, "fdf.tmbundle/Syntaxes/fdf.tmLanguage", plistData)
        AddFile(archive, "fdf.tmbundle/Preferences/Comments.tmPreferences", comments)
        AddFile(archive, "fdf.tmbundle/Preferences/Indentation Rules.tmPreferences", indentation)


def CheckVersion(version):
    with (HERE / "package.json").open(encoding="utf-8") as f:
        declared = json.load(f)["version"]

    if declared != version:
        sys.exit(f"editors/package.json says {declared}, release version is {version}")


def Main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    CheckVersion(args.version)
    args.out.mkdir(parents=True, exist_ok=True)

    grammar = LoadGrammar()
    plistPath = args.out / "fdf.tmLanguage"
    WritePlist(grammar, plistPath)
    plistData = plistPath.read_bytes()
    preferences = BuildPreferences(LoadLanguageConfiguration())

    BuildSublime(plistData, preferences, args.out / "fdf.sublime-package")
    BuildTmBundle(plistData, preferences, args.version, args.out / "fdf.tmbundle.zip")
    shutil.copyfile(HERE / "fdf.tmLanguage.json", args.out / "fdf.tmLanguage.json")

    for produced in sorted(args.out.iterdir()):
        print(f"{produced.name} ({produced.stat().st_size} bytes)")


if __name__ == "__main__":
    Main()
