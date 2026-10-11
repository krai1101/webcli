"""Relocate an installed runtime and create a ZIP with notices and checksums."""

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import zipfile
from pathlib import Path


def command(*args):
    return subprocess.check_output(args, text=True).strip()


def relocate(stage):
    files = [p for p in stage.iterdir() if p.is_file()]
    if platform.system() == "Linux":
        for path in files:
            with path.open('rb') as source:
                magic = source.read(4)
            if magic == b"\x7fELF":
                subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", str(path)], check=True)
    elif platform.system() == "Darwin":
        binaries = [p for p in files if p.name == "webcli" or p.suffix == ".dylib"]
        for path in binaries:
            for line in command("otool", "-L", str(path)).splitlines()[1:]:
                dependency = line.strip().split(" (", 1)[0]
                name = Path(dependency).name
                if (stage / name).is_file() and dependency != "@loader_path/" + name:
                    subprocess.run(["install_name_tool", "-change", dependency,
                                    "@loader_path/" + name, str(path)], check=True)
            if path.suffix == ".dylib":
                subprocess.run(["install_name_tool", "-id", "@rpath/" + path.name,
                                str(path)], check=True)
        for path in binaries:
            subprocess.run(["codesign", "--force", "--sign", "-", str(path)], check=True)


def dependency_sources(stage, vcpkg):
    if platform.system() == "Windows":
        return
    destination = stage / "dependency-sources"
    destination.mkdir(exist_ok=True)
    for package in ("libidn2", "libunistring"):
        downloads = Path(os.environ.get("VCPKG_DOWNLOADS", str(vcpkg / "downloads")))
        archives = list(downloads.glob(package + "-*.tar.*"))
        if len(archives) != 1:
            raise RuntimeError(f"Expected one matching source archive for {package}: {archives}")
        shutil.copy2(archives[0], destination)
        shutil.copytree(vcpkg / "ports" / package, destination / "recipes" / package, dirs_exist_ok=True)
    shutil.copytree(Path(__file__).parent / "triplets", destination / "triplets", dirs_exist_ok=True)
    (destination / "README.txt").write_text(
        "These are the matching upstream sources and vcpkg build recipes for the\n"
        "dynamically linked LGPL libraries. They may be replaced independently of\n"
        "WebCLI. The recipes use the vcpkg baseline recorded in BUILD.txt.\n",
        encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    parser.add_argument("--vcpkg", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", args.name):
        parser.error("Archive name must be a plain filename")
    if not re.fullmatch(r"v?\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?", args.version):
        parser.error("Invalid release version")
    stage = args.stage.resolve()
    executable = stage / ("webcli.exe" if platform.system() == "Windows" else "webcli")
    if not executable.is_file():
        parser.error("Installed executable is missing")
    relocate(stage)
    dependency_sources(stage, args.vcpkg.resolve())
    baseline = json.loads((Path(__file__).resolve().parent.parent / "vcpkg.json").read_text())["builtin-baseline"]
    if platform.system() != "Windows" and command("git", "-C", str(args.vcpkg), "rev-parse", "HEAD") != baseline:
        raise RuntimeError("Dependency sources must use the pinned vcpkg baseline")
    (stage / "BUILD.txt").write_text(
        f"WebCLI {args.version}\nPlatform: {args.name}\nvcpkg baseline: {baseline}\n"
        "Copyright 2026 krai1101\nSee LICENSE, THIRD_PARTY.md and licenses/.\n",
        encoding="utf-8")
    reported_version = command(str(executable), "--version")
    base = args.version.removeprefix("v").split("-", 1)[0]
    if reported_version != "WebCLI " + base:
        raise RuntimeError(f"Tag/build version mismatch: {reported_version}")
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / (args.name + ".zip")
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                output.write(path, Path(args.name) / path.relative_to(stage))
    digest = hashlib.sha256()
    with archive.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    archive.with_suffix(".zip.sha256").write_text(f"{digest.hexdigest()}  {archive.name}\n", encoding="utf-8")
    print(f"Created {archive} ({archive.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()
