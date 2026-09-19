#!/usr/bin/env python
"""M9-E E3 packaging helper (maintainer tool, NOT part of the normal build).

Builds the scripted portable ZIP from a Release deployed tree:
  Release deploy -> staging -> integrity/negative checks -> manifest
  -> ZIP -> fresh extraction -> manifest re-check -> minimal-PATH run.

Fail-fast: every gate exits non-zero on failure. Idempotent: staging,
ZIP and extraction directories are rebuilt from scratch on every run.
The ZIP is a SCRIPTED PORTABLE ZIP - byte reproducibility is NOT
claimed. This script is never invoked by the normal configure/build.

Usage:
  python scripts/make_package.py <release-build-dir> <deploy-dir>
e.g.
  python scripts/make_package.py build/release build/release/deploy
"""
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile

SOURCE_ROOT = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SOURCE_ROOT)
PACKAGE_ROOT = os.path.join(REPO_ROOT, "build", "package")
EXTRACT_ROOT = os.path.join(REPO_ROOT, "build", "package-extract")

REQUIRED_FILES = [
    "ModbusLens.exe",
    "platforms/qwindows.dll",
    "imageformats/qico.dll",
    "ModbusLens/qmldir",
    "ModbusLens/src/ui/qml/Main.qml",
    "ModbusLens/src/ui/qml/components/StatisticsOverview.qml",
    "samples/demo_v1.mlog",
]
FORBIDDEN_PATTERNS = [
    "CMakeFiles", "CMakeCache.txt", "build.ninja", ".ninja",
    "make_icon.py", "icon.svg", "ModbusLens.ico",
    "t014_protocol_error.mlog", "t015_broadcast.mlog",
    "t015_unsupported_fc08.mlog",
]
FORBIDDEN_SUFFIXES = [".o", ".obj", ".a", ".lib", ".pdb", ".cpp", ".h.in",
                      ".rc.in", ".py"]
SECRET_NAMES = [".env", "credentials", "secrets", "token"]
SECRET_PATTERNS = [b"OPENAI_API_KEY", b"ANTHROPIC_API_KEY", b"API_KEY=",
                   b"BEGIN PRIVATE KEY", b"Bearer "]
TEXT_SUFFIXES = [".txt", ".qml", ".mlog", ".json", ".js"]

README_TEMPLATE = """ModbusLens {version}
=====================

Portable Windows package (development/release candidate, unsigned).

How to launch
-------------
Double-click ModbusLens.exe (or run it from a console). No installer
and no development environment is required; all Qt runtime files ship
in this folder.

Samples
-------
samples/demo_v1.mlog contains a deterministic demo session: load it via
the Replay workspace (Load Replay) to explore the diagnostic views.

Notes
-----
- This package is not code-signed and has not been published.
- Version {version} (development candidate).
"""


def fail(message):
    print("make_package FAIL: " + message)
    sys.exit(1)


def read_authority_version(release_build_dir):
    """Derive the version from the Release build's configured authority."""
    header = os.path.join(release_build_dir, "generated",
                          "modbuslens_version.h")
    if not os.path.isfile(header):
        fail("generated version header missing: %s" % header)
    for line in open(header, encoding="utf-8"):
        if "MODBUSLENS_VERSION_STRING" in line:
            return line.split('"')[1]
    fail("MODBUSLENS_VERSION_STRING not found in %s" % header)


def pe_machine_and_version(exe_path):
    """Read the PE Machine field + VersionInfo strings.

    Machine via pefile; the string table via pefile with a fallback to the
    PowerShell VersionInfo shell query (which reads the same resource).
    """
    import pefile
    pe = pefile.PE(exe_path)
    machine = pe.FILE_HEADER.Machine
    strings = {}
    if hasattr(pe, "FileInfo"):
        for fileinfo in pe.FileInfo:
            for entry in fileinfo:
                if getattr(entry, "Key", b"") == b"StringFileInfo":
                    for table in entry.StringTable:
                        strings.update(dict(table.entries))
    if "ProductVersion" not in strings:
        helper = os.path.join(REPO_ROOT, "build", "e1_pe_inspect.ps1")
        result = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-File", helper, exe_path],
            capture_output=True, text=True)
        for line in result.stdout.splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                strings[key.strip()] = value
    if machine != 0x8664:
        fail("PE Machine is 0x%04X, expected AMD64 (0x8664) for the x64 "
             "package label" % machine)
    product = strings.get("ProductVersion", "")
    if product != AUTHORITY_VERSION:
        fail("PE ProductVersion %r != authority version %r"
             % (product, AUTHORITY_VERSION))
    return "x64", strings


def run_deploy(release_build_dir, deploy_dir):
    """Deploy the Release tree via the shared (parameterized) script.

    cmd drops empty quoted positional arguments, so QT_BIN/MINGW_BIN are
    derived here from the SAME CMakeCache the bat would use and passed
    explicitly.
    """
    bat = os.path.join(REPO_ROOT, "scripts", "deploy_windows.bat")
    cache = open(os.path.join(release_build_dir, "CMakeCache.txt"),
                 encoding="utf-8", errors="ignore").read()
    compiler = qt6_dir = None
    for line in cache.splitlines():
        if line.startswith("CMAKE_CXX_COMPILER:"):
            compiler = line.split("=", 1)[1]
        elif line.startswith("Qt6_DIR:"):
            qt6_dir = line.split("=", 1)[1]
    if not compiler or not qt6_dir:
        fail("CMakeCache is missing CMAKE_CXX_COMPILER/Qt6_DIR")
    mingw_bin = os.path.dirname(compiler)
    qt_bin = qt6_dir.replace("/lib/cmake/Qt6", "") + "/bin"
    if not os.path.isfile(os.path.join(qt_bin, "windeployqt.exe")):
        fail("windeployqt.exe not found in %s" % qt_bin)
    result = subprocess.run(
        ["cmd", "/c", os.path.normpath(bat),
         os.path.normpath(release_build_dir), os.path.normpath(qt_bin),
         os.path.normpath(mingw_bin), os.path.normpath(deploy_dir)],
        cwd=REPO_ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        fail("Release deploy failed: %s%s" % (result.stdout[-400:],
                                              result.stderr[-200:]))
    if "Deployment directory ready" not in result.stdout:
        fail("Release deploy did not report success: %s"
             % result.stdout[-400:])
    print("make_package: Release deploy OK -> %s" % deploy_dir)


def stage_package(deploy_dir, staging):
    if os.path.isdir(staging):
        shutil.rmtree(staging)
    shutil.copytree(deploy_dir, staging)
    # The Qt resource mirror of the brand ICO under the QML module dir is
    # redundant: the icon is embedded in the executable qrc and the runtime
    # reads it from there. The on-disk mirror is not shipped.
    assets_mirror = os.path.join(staging, "ModbusLens", "assets")
    if os.path.isdir(assets_mirror):
        shutil.rmtree(assets_mirror)
    readme = os.path.join(staging, "README.txt")
    with open(readme, "w", encoding="utf-8", newline="\r\n") as handle:
        handle.write(README_TEMPLATE.format(version=AUTHORITY_VERSION))
    print("make_package: staged %d entries + README.txt"
          % sum(len(files) for _, _, files in os.walk(staging)))


def structural_checks(staging):
    for required in REQUIRED_FILES:
        if not os.path.isfile(os.path.join(staging, required)):
            fail("required package file missing: %s" % required)
    for root, dirs, files in os.walk(staging):
        rel_root = os.path.relpath(root, staging)
        for name in dirs + files:
            rel = os.path.normpath(os.path.join(rel_root, name))
            for pattern in FORBIDDEN_PATTERNS:
                if pattern.lower() in rel.lower():
                    fail("forbidden package content: %s" % rel)
            for suffix in FORBIDDEN_SUFFIXES:
                if name.lower().endswith(suffix):
                    fail("forbidden package suffix: %s" % rel)
    for fixture in ["t014_protocol_error.mlog", "t015_broadcast.mlog",
                    "t015_unsupported_fc08.mlog"]:
        for root, _, files in os.walk(staging):
            if fixture in files:
                fail("regression fixture leaked into package: %s" % fixture)
    print("make_package: structural checks PASS (required present, "
          "forbidden absent, StatisticsOverview retained, samples policy "
          "PASS)")


def negative_scans(staging):
    for root, _, files in os.walk(staging):
        for name in files:
            lowered = name.lower()
            for secret_name in SECRET_NAMES:
                if secret_name in lowered:
                    fail("credential-like filename in package: %s" % name)
            suffix = os.path.splitext(name)[1].lower()
            if suffix in TEXT_SUFFIXES:
                blob = open(os.path.join(root, name), "rb").read()
                for pattern in SECRET_PATTERNS:
                    if pattern.lower() in blob.lower():
                        fail("credential-like pattern %r in %s"
                             % (pattern, name))
                lowered_blob = blob.lower()
                if (b"e:" + bytes([47]) + b"desktop" in lowered_blob
                        or b"e:" + bytes([92]) + b"desktop" in lowered_blob):
                    fail("absolute machine path in text file: %s" % name)
    print("make_package: credential/config negative scan PASS "
          "(known-risk filenames and text patterns; not a mathematical "
          "proof of binary secrets)")
    print("make_package: absolute-path negative audit PASS "
          "(package text files)")


def write_manifest(staging):
    manifest_path = os.path.join(staging, "package-manifest.sha256")
    entries = []
    for root, _, files in os.walk(staging):
        for name in files:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, staging).replace(os.sep, "/")
            if rel == "package-manifest.sha256":
                continue
            digest = hashlib.sha256(open(full, "rb").read()).hexdigest()
            entries.append((rel, digest))
    entries.sort()
    with open(manifest_path, "w", encoding="utf-8", newline="\n") as handle:
        for rel, digest in entries:
            handle.write("%s  %s\n" % (digest, rel))
    print("make_package: manifest written (%d payload files)"
          % len(entries))


def make_zip(staging, stem):
    zip_path = os.path.join(PACKAGE_ROOT, stem + ".zip")
    if os.path.isfile(zip_path):
        os.remove(zip_path)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as archive:
        for root, _, files in os.walk(staging):
            for name in files:
                full = os.path.join(root, name)
                rel = os.path.relpath(full, staging)
                archive.write(full, os.path.join(stem, rel))
    size = os.path.getsize(zip_path)
    digest = hashlib.sha256(open(zip_path, "rb").read()).hexdigest()
    print("make_package: ZIP %s (%d bytes, sha256=%s)"
          % (zip_path, size, digest))
    return zip_path, size, digest


def verify_zip_entries(zip_path, staging, stem):
    with zipfile.ZipFile(zip_path) as archive:
        zip_entries = set(archive.namelist())
    expected = set()
    for root, _, files in os.walk(staging):
        for name in files:
            rel = os.path.relpath(os.path.join(root, name), staging)
            expected.add(os.path.join(stem, rel).replace(os.sep, "/"))
    if zip_entries != expected:
        missing = expected - zip_entries
        extra = zip_entries - expected
        fail("ZIP entries mismatch (missing=%s extra=%s)"
             % (sorted(missing)[:5], sorted(extra)[:5]))
    print("make_package: ZIP entries == staging file set (%d entries)"
          % len(expected))


def extract_and_verify(zip_path, stem):
    extract_dir = os.path.join(EXTRACT_ROOT, stem)
    if os.path.isdir(extract_dir):
        shutil.rmtree(extract_dir)
    with zipfile.ZipFile(zip_path) as archive:
        archive.extractall(EXTRACT_ROOT)
    for root, _, files in os.walk(extract_dir):
        for name in files:
            if name == "package-manifest.sha256":
                continue
            full = os.path.join(root, name)
            rel = os.path.relpath(full, extract_dir).replace(os.sep, "/")
            digest = hashlib.sha256(open(full, "rb").read()).hexdigest()
            manifest_line = "%s  %s" % (digest, rel)
            manifest = open(os.path.join(extract_dir,
                                         "package-manifest.sha256"),
                            encoding="utf-8").read()
            if manifest_line not in manifest:
                fail("extracted file mismatch vs manifest: %s" % rel)
    print("make_package: fresh extraction verified against manifest "
          "(%s)" % extract_dir)
    return extract_dir


def minimal_path_run(extract_dir):
    exe = os.path.join(extract_dir, "ModbusLens.exe")
    env = dict(os.environ)
    env["PATH"] = r"C:\Windows\System32;C:\Windows"
    env["QT_ASSUME_STDERR_HAS_CONSOLE"] = "1"
    for mode in ["--qml-smoke-test", "--qml-nav-check",
                 "--qml-geometry-check"]:
        result = subprocess.run([exe, mode], env=env, cwd=REPO_ROOT,
                                capture_output=True, text=True,
                                errors="replace", timeout=300)
        if result.returncode != 0:
            fail("minimal-PATH %s failed on extracted package (%d)"
                 % (mode, result.returncode))
        blob = ((result.stdout or "") + (result.stderr or "")).lower()
        for warning in ["referenceerror", "typeerror", "binding loop",
                        "qimagereader", "plugin", "missing dll"]:
            if warning in blob:
                fail("minimal-PATH %s emitted warning containing %r"
                     % (mode, warning))
        print("make_package: minimal-PATH extracted %s PASS" % mode)


def external_cwd_run(extract_dir):
    exe = os.path.join(extract_dir, "ModbusLens.exe")
    env = dict(os.environ)
    env["PATH"] = r"C:\Windows\System32;C:\Windows"
    env["QT_ASSUME_STDERR_HAS_CONSOLE"] = "1"
    result = subprocess.run([exe, "--qml-smoke-test"], env=env,
                            cwd=os.path.expanduser("~"),
                            capture_output=True, text=True,
                            errors="replace", timeout=300)
    if result.returncode != 0:
        fail("external-CWD launch failed (%d)" % result.returncode)
    print("make_package: external-CWD launch PASS (working-directory "
          "independence)")


AUTHORITY_VERSION = ""
ARCH_LABEL = ""


def main():
    if len(sys.argv) != 3:
        fail("usage: make_package.py <release-build-dir> <release-deploy-dir>")
    global AUTHORITY_VERSION
    release_build_dir = os.path.abspath(sys.argv[1])
    deploy_dir = os.path.abspath(sys.argv[2])
    if not os.path.isfile(os.path.join(release_build_dir, "CMakeCache.txt")):
        fail("Release CMakeCache.txt not found in %s" % release_build_dir)
    AUTHORITY_VERSION = read_authority_version(release_build_dir)
    exe = os.path.join(deploy_dir, "ModbusLens.exe")
    if not os.path.isfile(exe):
        run_deploy(release_build_dir, deploy_dir)
    arch_label, _ = pe_machine_and_version(exe)
    stem = "ModbusLens-%s-windows-%s" % (AUTHORITY_VERSION, arch_label)
    print("make_package: stem = %s" % stem)
    staging = os.path.join(PACKAGE_ROOT, stem)
    stage_package(deploy_dir, staging)
    structural_checks(staging)
    negative_scans(staging)
    write_manifest(staging)
    zip_path, size, digest = make_zip(staging, stem)
    verify_zip_entries(zip_path, staging, stem)
    extract_dir = extract_and_verify(zip_path, stem)
    minimal_path_run(extract_dir)
    external_cwd_run(extract_dir)
    print("make_package PASS: %s (zip %d bytes, sha256 %s)"
          % (stem, size, digest))


if __name__ == "__main__":
    main()
