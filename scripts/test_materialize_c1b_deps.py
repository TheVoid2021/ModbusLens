#!/usr/bin/env python3
"""M12-C C1b dependency materializer contract tests (DEP01-DEP12).

These tests never touch the network: they exercise the offline materializer against local
distfiles and deliberately tampered copies, and they assert that a tampered or wrong
artifact can never be accepted.

Run:  python scripts/test_materialize_c1b_deps.py
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MATERIALIZER = os.path.join(REPO_ROOT, "scripts", "materialize_c1b_deps.py")
PYTHON = sys.executable

# The CTest registration forwards the very same absolute paths the build consumes
# (--lock / --distfiles / --zlib-include / --zlib-library). Consume them here so the
# contract tests verify the exact files the product uses, independent of the CTest
# working directory. Everything else derives from __file__ (no cwd, no PATH probing).
_ZLIB_KEYS = ("--zlib-include", "--zlib-library")
_LOCK_KEY = "--lock"
_DISTFILES_KEY = "--distfiles"


def _cli_overrides() -> dict:
    overrides: dict = {}
    argv = sys.argv[1:]
    for index, token in enumerate(argv):
        if token in (_LOCK_KEY, _DISTFILES_KEY) + _ZLIB_KEYS and index + 1 < len(argv):
            overrides[token] = os.path.abspath(argv[index + 1])
    return overrides


CLI = _cli_overrides()
LOCK = CLI.get(_LOCK_KEY, os.path.join(REPO_ROOT, "third_party", "m12c-c1b",
                                       "dependencies.lock.json"))
DISTFILES = CLI.get(_DISTFILES_KEY, os.path.join(REPO_ROOT, "third_party", "m12c-c1b",
                                                 "distfiles"))

results: list[tuple[str, bool, str]] = []


def record(case: str, ok: bool, detail: str) -> None:
    results.append((case, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {case}  {detail}", flush=True)


MANIFEST = os.path.join(REPO_ROOT, "build", "deps", "m12c-c1b", "manifest",
                        "materialized.json")


def zlib_inputs() -> list[str]:
    """Explicit zlib inputs only (never probed from PATH).

    The values forwarded by CMake's add_test (CLI) take precedence; the git-ignored
    materialized manifest is the fallback for direct invocations.
    """
    if all(key in CLI for key in _ZLIB_KEYS):
        return ["--zlib-include", CLI["--zlib-include"],
                "--zlib-library", CLI["--zlib-library"]]
    if os.path.isfile(MANIFEST):
        with open(MANIFEST, "r", encoding="utf-8") as handle:
            zlib_pin = json.load(handle)["zlib"]
        return ["--zlib-include", zlib_pin["header"],
                "--zlib-library", zlib_pin["library"]]
    return []


def run_materializer(*extra: str, expect_rc: int | None = None,
                     cwd: str | None = None) -> subprocess.CompletedProcess:
    # ALWAYS forward the absolute lock/distfiles identity (never the materializer's
    # cwd-relative defaults): the CTest cwd is the build directory, where relative
    # paths silently resolve to nothing.
    command = [PYTHON, MATERIALIZER, "--lock", LOCK, "--distfiles", DISTFILES,
               *zlib_inputs(), *extra]
    completed = subprocess.run(command, capture_output=True, text=True, cwd=cwd)
    if expect_rc is not None and completed.returncode != expect_rc:
        raise AssertionError(
            f"expected rc={expect_rc} got rc={completed.returncode}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}")
    return completed


def load_lock() -> dict:
    with open(LOCK, "r", encoding="utf-8") as handle:
        return json.load(handle)


def sha256_of(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def dep01_pdfium_hash_verified() -> None:
    completed = run_materializer("--verify-only", expect_rc=0)
    ok = "verified pdfium artifact sha256=" in completed.stdout
    record("DEP01", ok, "correct PDFium distfile hash PASS (verify-only rc=0)")


def dep02_tampered_pdfium_rejected() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        shutil.copyfile(os.path.join(DISTFILES, "pdfium-win-x64.tgz"),
                        os.path.join(tmp, "pdfium-win-x64.tgz"))
        shutil.copyfile(os.path.join(DISTFILES, "libzip-1.11.4.tar.gz"),
                        os.path.join(tmp, "libzip-1.11.4.tar.gz"))
        target = os.path.join(tmp, "pdfium-win-x64.tgz")
        with open(target, "r+b") as handle:
            handle.seek(1024)
            byte = handle.read(1)
            handle.seek(1024)
            handle.write(bytes([byte[0] ^ 0xFF]))
        completed = run_materializer("--verify-only", "--distfiles", tmp, expect_rc=4)
        ok = "hash mismatch" in completed.stderr and "pdfium" in completed.stderr
        record("DEP02", ok, "tampered PDFium distfile FAIL (rc=4, hash mismatch)")


def dep03_libzip_hash_verified() -> None:
    completed = run_materializer("--verify-only", expect_rc=0)
    ok = "verified libzip archive sha256=" in completed.stdout
    record("DEP03", ok, "correct libzip archive hash PASS (verify-only rc=0)")


def dep04_tampered_libzip_rejected() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        shutil.copyfile(os.path.join(DISTFILES, "pdfium-win-x64.tgz"),
                        os.path.join(tmp, "pdfium-win-x64.tgz"))
        shutil.copyfile(os.path.join(DISTFILES, "libzip-1.11.4.tar.gz"),
                        os.path.join(tmp, "libzip-1.11.4.tar.gz"))
        target = os.path.join(tmp, "libzip-1.11.4.tar.gz")
        with open(target, "a") as handle:
            handle.write("tampered")
        completed = run_materializer("--verify-only", "--distfiles", tmp, expect_rc=4)
        ok = "hash mismatch" in completed.stderr and "libzip" in completed.stderr
        record("DEP04", ok, "tampered libzip archive FAIL (rc=4, hash mismatch)")


def dep05_missing_distfile_actionable() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        completed = run_materializer("--verify-only", "--distfiles", tmp, expect_rc=3)
        ok = ("missing distfile" in completed.stderr
              and "never downloads" in completed.stderr)
        record("DEP05", ok, "missing distfile FAIL with actionable message (rc=3)")


def dep06_no_network_path() -> None:
    with open(MATERIALIZER, "r", encoding="utf-8") as handle:
        source = handle.read()
    forbidden = ["urllib", "requests", "socket", "http.client", "curl ", "wget ",
                 "urlretrieve", "pip install"]
    hits = [token for token in forbidden if token in source]
    ok = not hits
    record("DEP06", ok,
           "materializer has no network path (no urllib/requests/socket/curl/wget)")


def dep07_exact_pin_manifest() -> None:
    lock = load_lock()
    pdfium = lock["dependencies"]["pdfium"]
    libzip = lock["dependencies"]["libzip"]
    ok = (
        pdfium["version"] == "156.0.8066.0"
        and pdfium["distributorTag"] == "chromium/8066"
        and pdfium["upstreamCommit"] == "fc46361ce75055cd549cb938fae5d6a3fe3a1a05"
        and pdfium["artifactSha256"]
        == "739a57d597d864297909cc40a2411eba728490c76a0fa25e3ea299c7f6b07020"
        and pdfium["runtimeDllSha256"]
        == "d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b"
        and libzip["version"] == "1.11.4"
        and libzip["archiveSha256"]
        == "82e9f2f2421f9d7c2466bbc3173cd09595a88ea37db0d559a9d0a2dc60dc722e"
        and lock["dependencies"]["zlib"]["version"] == "1.2.13"
    )
    record("DEP07", ok, "lock manifest pins are the frozen ones")


def dep08_wrong_version_rejected() -> None:
    lock = load_lock()
    lock["dependencies"]["pdfium"]["expectedVersionFile"]["MAJOR"] = "999"
    with tempfile.TemporaryDirectory() as tmp:
        lock_path = os.path.join(tmp, "lock.json")
        with open(lock_path, "w", encoding="utf-8") as handle:
            json.dump(lock, handle)
        root = os.path.join(tmp, "root")
        completed = run_materializer("--lock", lock_path, "--root", root, "--no-build",
                                     expect_rc=4)
        ok = "VERSION" in completed.stderr
        record("DEP08", ok, "wrong expected VERSION is rejected (hash path untouched)")


def dep09_materialized_dll_hash() -> None:
    lock = load_lock()
    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "root")
        run_materializer("--root", root, "--no-build", expect_rc=0)
        with open(os.path.join(root, "manifest", "materialized.json"),
                  "r", encoding="utf-8") as handle:
            manifest = json.load(handle)
        dll = os.path.join(REPO_ROOT, manifest["pdfium"]["runtimeDll"])
        actual = sha256_of(dll)
        ok = actual == lock["dependencies"]["pdfium"]["runtimeDllSha256"]
        record("DEP09", ok, f"materialized pdfium.dll sha256={actual}")


def dep10_runtime_dll_staged_locally() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "root")
        run_materializer("--root", root, "--no-build", expect_rc=0)
        expected = os.path.join(root, "pdfium", "bin", "pdfium.dll")
        ok = os.path.isfile(expected) and os.path.getsize(expected) > 0
        record("DEP10", ok, "runtime DLL present in the materialized root (no PATH reliance)")


def dep11_license_material_retained() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "root")
        completed = run_materializer("--root", root, "--no-build", expect_rc=0)
        license_file = os.path.join(root, "pdfium", "LICENSE")
        notices = os.path.join(root, "pdfium", "licenses")
        count = len(os.listdir(notices)) if os.path.isdir(notices) else 0
        ok = (os.path.isfile(license_file) and count >= 14
              and "license material retained" in completed.stdout)
        record("DEP11", ok, f"LICENSE + {count} third-party notices retained")


def dep12_idempotent_rerun() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "root")
        run_materializer("--root", root, "--no-build", expect_rc=0)
        first = snapshot(root)
        completed = run_materializer("--root", root, "--no-build", expect_rc=0)
        second = snapshot(root)
        ok = first == second and "already materialized" in completed.stdout
        record("DEP12", ok, f"{len(first)} files identical across reruns (stamp short-circuit)")


def snapshot(root: str) -> dict:
    listing = {}
    for base, _dirs, files in os.walk(root):
        for name in files:
            path = os.path.join(base, name)
            listing[os.path.relpath(path, root).replace("\\", "/")] = sha256_of(path)
    return listing


def main() -> int:
    dep01_pdfium_hash_verified()
    dep02_tampered_pdfium_rejected()
    dep03_libzip_hash_verified()
    dep04_tampered_libzip_rejected()
    dep05_missing_distfile_actionable()
    dep06_no_network_path()
    dep07_exact_pin_manifest()
    dep08_wrong_version_rejected()
    dep09_materialized_dll_hash()
    dep10_runtime_dll_staged_locally()
    dep11_license_material_retained()
    dep12_idempotent_rerun()

    failed = [case for case, ok, _ in results if not ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} dependency materializer tests passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
