#!/usr/bin/env python
"""M9-E E3 packaging helper (maintainer tool, NOT part of the normal build).

POST-M12-REL-R2 (T027 §114, Human decisions R1-R3): the CANONICAL
packaging entry is the candidate-tree mode. The canonical flow is

    verified behavior source
    -> canonical candidate generation (modbuslens_generate_candidate.cmake)
    -> candidate/ModbusLens
    -> canonical packaging (this script, --candidate mode)
    -> staging / manifest / ZIP / checksums

The candidate root is an IMMUTABLE package input: it is validated against
its own candidate-manifest.json (every listed file present, SHA-256
match), never repaired, never re-derived through windeployqt or
scripts/deploy_windows.bat, and never mixed with raw build output.
pdfium.dll is a REQUIRED release runtime (R2) and synthetic Manual
samples (samples/ModbusLens_Test_Manual_*) must never enter staging or
the ZIP (R3, fail-closed).

Usage (canonical):
  python scripts/make_package.py --candidate <candidate-root>
e.g.
  python scripts/make_package.py --candidate build/release/candidate/ModbusLens

The historical two-argument entry below is the M9-E/M10/M11 LEGACY
procedure (Release build dir + windeployqt deploy dir). It remains for
provenance and its tests; it is NOT the canonical release path anymore
and must not be used for post-M12 canonical packaging.

Legacy behavior (unchanged): builds the scripted portable ZIP from a
Release deployed tree. Fail-fast: every gate exits non-zero on failure.
Idempotent: staging, ZIP and extraction directories are rebuilt from
scratch on every run. The ZIP is a SCRIPTED PORTABLE ZIP - byte
reproducibility is NOT claimed. This script is never invoked by the
normal configure/build.

Legacy usage:
  python scripts/make_package.py <release-build-dir> <deploy-dir>
e.g.
  python scripts/make_package.py build/release build/release/deploy
"""
import hashlib
import json
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
                      ".rc.in", ".py", ".prl", ".qrc"]
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


def sha256_file(path):
    """Content identity of a file (used for every freshness decision).

    Deliberately NOT mtime and NOT existence: a file can be present and
    "recent" while still carrying a different build's bytes, and a copy can
    preserve its size while changing its content.
    """
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def deploy_is_current(release_build_dir, deploy_dir):
    """True only when the deployed exe is byte-identical to the build tree.

    The deployed client must provably come from the build directory given on
    the command line. "ModbusLens.exe exists in the deploy directory" does NOT
    establish that: a previous run may have left a binary from an older build,
    and reusing it would silently package the wrong product.
    """
    source = os.path.join(release_build_dir, "modbuslens.exe")
    deployed = os.path.join(deploy_dir, "ModbusLens.exe")
    if not os.path.isfile(source):
        fail("Release build exe not found: %s" % source)
    if not os.path.isfile(deployed):
        return False
    return sha256_file(source) == sha256_file(deployed)


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


def structural_checks(staging, required_files=None):
    """Structural gate shared by both packaging paths.

    `required_files` defaults to the LEGACY M9-E list (historical entry).
    The canonical candidate path passes CANDIDATE_REQUIRED_FILES instead.
    The R3 rule (no synthetic Manual sample) is enforced for BOTH paths.
    """
    required = REQUIRED_FILES if required_files is None else required_files
    for req in required:
        if not os.path.isfile(os.path.join(staging, req)):
            fail("required package file missing: %s" % req)
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
            if name.lower().startswith("modbuslens_test_manual_"):
                fail("synthetic Manual sample must not enter the package: "
                     "%s" % rel)
    for fixture in ["t014_protocol_error.mlog", "t015_broadcast.mlog",
                    "t015_unsupported_fc08.mlog"]:
        for root, _, files in os.walk(staging):
            if fixture in files:
                fail("regression fixture leaked into package: %s" % fixture)
    print("make_package: structural checks PASS (required present, "
          "forbidden absent, samples policy PASS)")


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
    os.makedirs(os.path.dirname(zip_path), exist_ok=True)
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


def verify_tree_against_manifest(root_dir):
    """Fail-fast re-check: every payload file must match the manifest."""
    manifest_path = os.path.join(root_dir, "package-manifest.sha256")
    manifest = open(manifest_path, encoding="utf-8").read()
    for root, _, files in os.walk(root_dir):
        for name in files:
            if name == "package-manifest.sha256":
                continue
            full = os.path.join(root, name)
            rel = os.path.relpath(full, root_dir).replace(os.sep, "/")
            digest = hashlib.sha256(open(full, "rb").read()).hexdigest()
            if ("%s  %s" % (digest, rel)) not in manifest:
                fail("payload mismatch vs manifest: %s" % rel)


def extract_and_verify(zip_path, stem):
    extract_dir = os.path.join(EXTRACT_ROOT, stem)
    if os.path.isdir(extract_dir):
        shutil.rmtree(extract_dir)
    with zipfile.ZipFile(zip_path) as archive:
        archive.extractall(EXTRACT_ROOT)
    verify_tree_against_manifest(extract_dir)
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

# ---------------------------------------------------------------------------
# POST-M12-REL-R2 canonical candidate-tree packaging (T027 §114, R1-R3).
#
# The candidate root produced by cmake/modbuslens_generate_candidate.cmake
# is the ONLY package input. This path never invokes windeployqt or
# scripts/deploy_windows.bat, never reads the raw build tree for runtime
# files, and never repairs the candidate: anything wrong fails closed.
# ---------------------------------------------------------------------------
CANDIDATE_GENERATOR = "modbuslens_generate_candidate.cmake"
CANDIDATE_MANIFEST_NAME = "candidate-manifest.json"
# The candidate manifest is build provenance, not product runtime: it is
# consumed for validation and intentionally NOT shipped. This is the single
# explicit exclusion; everything else manifest-listed ships unchanged
# (modbuslens.exe is renamed to its historical product name ModbusLens.exe,
# same bytes).
CANDIDATE_EXCLUDED_FROM_PACKAGE = ["candidate-manifest.json"]
# M9-E canonical content rule (kept for the candidate path): the brand
# ICO mirror under the QML module dir is redundant - the icon is embedded
# in the executable qrc and the runtime reads it from there, and the
# on-disk mirror has never been shipped. (The legacy flow deleted
# ModbusLens/assets from the windeployqt staging; the candidate layout
# keeps the mirror under qml/ModbusLens/assets, so the candidate path
# excludes it explicitly and the structural gate re-checks it.)
CANDIDATE_EXCLUDED_PREFIXES = ["qml/ModbusLens/assets/"]
# Developer/build artifacts that the Qt kit's QML module tree carries but
# that have never been runtime content (import libraries, qmake metadata,
# compiled objects, resource-list sources). The suffix list is the canonical
# FORBIDDEN_SUFFIXES plus the qmake-only kinds observed in the Qt kit's
# shipped qml/ closure; the candidate staging skips them explicitly and the
# structural gate re-checks that none remain.
CANDIDATE_EXCLUDED_SUFFIXES = FORBIDDEN_SUFFIXES
# R2: the release runtime set that the candidate contract guarantees. The
# full content set comes from the candidate manifest itself; this list is
# the fail-closed floor (missing any of it stops packaging).
CANDIDATE_REQUIRED_RUNTIME = [
    "modbuslens.exe",
    "pdfium.dll",
    "platforms/qwindows.dll",
    "qt.conf",
]
# Required content of the canonical package AFTER staging (renames and the
# explicit release-only sample inclusion included).
CANDIDATE_REQUIRED_PACKAGE_FILES = [
    "ModbusLens.exe",
    "pdfium.dll",
    "platforms/qwindows.dll",
    "qt.conf",
    "qml/ModbusLens/qmldir",
    "samples/demo_v1.mlog",
    "README.txt",
]


def candidate_authority_version(repo_root):
    """Derive the package version from the repo's single CMake source.

    The candidate tree carries no version header (it is not a build tree),
    so the authority is the same CMake project VERSION the generated
    modbuslens_version.h is derived from. The PE ProductVersion of the
    packaged exe is cross-checked against this value in candidate mode.
    """
    cmake_lists = os.path.join(repo_root, "CMakeLists.txt")
    if not os.path.isfile(cmake_lists):
        fail("CMakeLists.txt not found next to the packaging script")
    for line in open(cmake_lists, encoding="utf-8", errors="ignore"):
        stripped = line.strip()
        if stripped.startswith("VERSION "):
            return stripped.split()[1]
    fail("project VERSION not found in %s" % cmake_lists)


def load_candidate_manifest(candidate_root):
    """Fail-closed load of the candidate root's own manifest."""
    path = os.path.join(candidate_root, CANDIDATE_MANIFEST_NAME)
    if not os.path.isfile(path):
        fail("candidate manifest missing: %s" % path)
    try:
        with open(path, encoding="utf-8") as handle:
            doc = json.load(handle)
    except ValueError as exc:
        fail("candidate manifest is not valid JSON: %s" % exc)
    if not isinstance(doc, dict):
        fail("candidate manifest is not a JSON object")
    if doc.get("generated-by") != CANDIDATE_GENERATOR:
        fail("candidate manifest was not generated by %s (got %r)"
             % (CANDIDATE_GENERATOR, doc.get("generated-by")))
    files = doc.get("files")
    if not isinstance(files, list) or not files:
        fail("candidate manifest lists no files")
    for entry in files:
        if not isinstance(entry, dict):
            fail("candidate manifest entry is not an object: %r" % (entry,))
        rel = entry.get("path")
        digest = entry.get("sha256")
        if not isinstance(rel, str) or not rel:
            fail("candidate manifest entry has no path: %r" % (entry,))
        if not isinstance(digest, str) or len(digest) != 64 \
                or any(c not in "0123456789abcdef" for c in digest.lower()):
            fail("candidate manifest entry has a malformed sha256: %s"
                 % rel)
        normalized = os.path.normpath(rel).replace(os.sep, "/")
        if (normalized != rel or rel.startswith("/")
                or rel.startswith("..") or ":" in rel):
            fail("unsafe candidate manifest path: %s" % rel)
    return doc


def verify_candidate_root(candidate_root, manifest):
    """Every manifest-listed file must exist with the manifest's SHA-256.

    Fail-closed floor (R2): the required M12 release runtime must be part
    of the candidate set. Missing/mismatched anything stops packaging.
    """
    listed = set()
    for entry in manifest["files"]:
        rel = entry["path"]
        full = os.path.join(candidate_root, *rel.split("/"))
        if not os.path.isfile(full):
            fail("candidate file listed in manifest is missing: %s" % rel)
        if sha256_file(full) != entry["sha256"]:
            fail("candidate file hash mismatch vs manifest: %s" % rel)
        listed.add(rel)
    for required in CANDIDATE_REQUIRED_RUNTIME:
        if required not in listed:
            fail("candidate root is missing required release runtime: %s"
                 % required)
    for entry in manifest["files"]:
        base = os.path.basename(entry["path"]).lower()
        if base.startswith("modbuslens_test_manual_"):
            fail("synthetic Manual sample must not be packaged: %s"
                 % entry["path"])
    return listed


def stage_candidate_package(candidate_root, manifest, version, staging):
    """Build the release staging tree from the validated candidate root.

    Content set = manifest-listed candidate files (minus the explicit
    exclusion) + the canonical sample (existing policy, R3 keeps it) +
    the generated README. Nothing else; no repair, no fallback.
    """
    if os.path.isdir(staging):
        shutil.rmtree(staging)
    os.makedirs(staging)
    for entry in manifest["files"]:
        rel = entry["path"]
        if rel in CANDIDATE_EXCLUDED_FROM_PACKAGE:
            continue
        if any(rel.startswith(prefix)
               for prefix in CANDIDATE_EXCLUDED_PREFIXES):
            continue
        if os.path.splitext(rel)[1].lower() in CANDIDATE_EXCLUDED_SUFFIXES:
            continue
        src = os.path.join(candidate_root, *rel.split("/"))
        dst_rel = "ModbusLens.exe" if rel == "modbuslens.exe" else rel
        dst = os.path.join(staging, *dst_rel.split("/"))
        parent = os.path.dirname(dst)
        if parent:
            os.makedirs(parent, exist_ok=True)
        shutil.copyfile(src, dst)
    demo_src = os.path.join(REPO_ROOT, "samples", "demo_v1.mlog")
    if not os.path.isfile(demo_src):
        fail("canonical sample missing from the repository: %s" % demo_src)
    os.makedirs(os.path.join(staging, "samples"), exist_ok=True)
    shutil.copyfile(demo_src, os.path.join(staging, "samples",
                                           "demo_v1.mlog"))
    readme = os.path.join(staging, "README.txt")
    with open(readme, "w", encoding="utf-8", newline="\r\n") as handle:
        handle.write(README_TEMPLATE.format(version=version))
    structural_checks(staging, CANDIDATE_REQUIRED_PACKAGE_FILES)
    negative_scans(staging)
    print("make_package: staged %d entries + README.txt (candidate tree)"
          % sum(len(files) for _, _, files in os.walk(staging)))


def package_candidate_root(candidate_root, output_root=PACKAGE_ROOT):
    """Deterministic candidate-tree packaging WITHOUT the runtime gates.

    Validates the candidate root, stages it, writes the package manifest,
    builds the ZIP, verifies the entry set, and re-extracts it against the
    manifest. Returns the package identity. This function alone does NOT
    constitute an accepted package: the canonical CLI additionally runs
    the PE identity check and the extracted minimal-PATH / external-CWD
    runtime gates on the real application.
    """
    manifest = load_candidate_manifest(candidate_root)
    verify_candidate_root(candidate_root, manifest)
    version = candidate_authority_version(REPO_ROOT)
    stem = "ModbusLens-%s-windows-x64" % version
    staging = os.path.join(output_root, stem)
    stage_candidate_package(candidate_root, manifest, version, staging)
    write_manifest(staging)
    zip_path, size, digest = make_zip(staging, stem)
    verify_zip_entries(zip_path, staging, stem)
    extract_dir = extract_and_verify(zip_path, stem)
    return {
        "stem": stem,
        "version": version,
        "staging": staging,
        "zip_path": zip_path,
        "zip_size": size,
        "zip_sha256": digest,
        "extract_dir": extract_dir,
        "candidate_root": candidate_root,
    }


def run_candidate_mode(candidate_root):
    """Canonical CLI entry: full pipeline including the runtime gates."""
    if not os.path.isdir(candidate_root):
        fail("candidate root not found: %s" % candidate_root)
    global AUTHORITY_VERSION
    AUTHORITY_VERSION = candidate_authority_version(REPO_ROOT)
    result = package_candidate_root(candidate_root)
    # The exe a user runs after unzipping must be byte-identical to the
    # candidate application (same bytes, historical product name).
    extracted_exe = os.path.join(result["extract_dir"], "ModbusLens.exe")
    candidate_exe = os.path.join(candidate_root, "modbuslens.exe")
    if not os.path.isfile(extracted_exe):
        fail("extracted exe missing: %s" % extracted_exe)
    if sha256_file(extracted_exe) != sha256_file(candidate_exe):
        fail("extracted exe does not match the candidate exe (%s)"
             % candidate_exe)
    print("make_package: extract identity OK vs candidate (sha256=%s)"
          % sha256_file(extracted_exe))
    pe_machine_and_version(os.path.join(result["staging"],
                                        "ModbusLens.exe"))
    minimal_path_run(result["extract_dir"])
    external_cwd_run(result["extract_dir"])
    print("make_package PASS: %s (zip %d bytes, sha256 %s)"
          % (result["stem"], result["zip_size"], result["zip_sha256"]))


def main():
    argv = sys.argv[1:]
    if argv and argv[0] == "--candidate":
        if len(argv) != 2:
            fail("usage: make_package.py --candidate <candidate-root>")
        run_candidate_mode(os.path.abspath(argv[1]))
        return
    # HISTORICAL legacy entry (M9-E/M10/M11): Release build dir + windeployqt
    # deploy dir. Not the canonical post-M12 packaging path (see module
    # docstring); retained for provenance and its tests.
    if len(argv) != 2:
        fail("usage: make_package.py --candidate <candidate-root> "
             "(canonical) or <release-build-dir> <release-deploy-dir> "
             "(legacy)")
    global AUTHORITY_VERSION
    release_build_dir = os.path.abspath(argv[0])
    deploy_dir = os.path.abspath(argv[1])
    if not os.path.isfile(os.path.join(release_build_dir, "CMakeCache.txt")):
        fail("Release CMakeCache.txt not found in %s" % release_build_dir)
    AUTHORITY_VERSION = read_authority_version(release_build_dir)
    exe = os.path.join(deploy_dir, "ModbusLens.exe")
    # Freshness is a CONTENT property, not an existence property. A deploy
    # directory left over from an earlier run can hold a different build's
    # binary; reusing it would package the wrong product with no error at all.
    if not deploy_is_current(release_build_dir, deploy_dir):
        if os.path.isfile(exe):
            print("make_package: deploy exe is STALE -> redeploying")
        run_deploy(release_build_dir, deploy_dir)
    # Post-condition: whatever happened above, the deployed client must now be
    # byte-identical to THIS build. Refuse to package anything else.
    if not deploy_is_current(release_build_dir, deploy_dir):
        fail("deployed exe still does not match %s after deploy"
             % os.path.join(release_build_dir, "modbuslens.exe"))
    print("make_package: deploy identity OK (sha256=%s)" % sha256_file(exe))
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
    # Close the identity chain end to end: the exe a user would actually run
    # after unzipping must be the same bytes as the build directory's exe.
    extracted_exe = os.path.join(extract_dir, "ModbusLens.exe")
    if not os.path.isfile(extracted_exe):
        fail("extracted exe missing: %s" % extracted_exe)
    build_exe = os.path.join(release_build_dir, "modbuslens.exe")
    if sha256_file(extracted_exe) != sha256_file(build_exe):
        fail("extracted exe does not match the build tree exe (%s)" % build_exe)
    print("make_package: extract identity OK (sha256=%s)"
          % sha256_file(extracted_exe))
    minimal_path_run(extract_dir)
    external_cwd_run(extract_dir)
    print("make_package PASS: %s (zip %d bytes, sha256 %s)"
          % (stem, size, digest))


if __name__ == "__main__":
    main()
