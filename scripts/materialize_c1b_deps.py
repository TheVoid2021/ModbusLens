#!/usr/bin/env python3
"""M12-C C1b offline dependency materializer.

Materializes the exact-pinned PDF/DOCX extraction dependencies of M12-C C1b from LOCAL
distfiles only. This script NEVER touches the network: there is no downloader here at all.

Contract (frozen in docs/tasks/T027 section 58.4):

  1. tracked lock manifest : third_party/m12c-c1b/dependencies.lock.json (pins + hashes only,
                             no machine-absolute paths)
  2. local distfiles only  : the pinned archives must already exist in the distfiles directory
  3. verify then act       : every distfile is SHA-256 verified BEFORE unpacking or building;
                             any mismatch is a hard failure
  4. materialized root     : build/deps/m12c-c1b/ (git-ignored build artifact)

Usage (run from the repository root):

  python scripts/materialize_c1b_deps.py                 # verify + unpack + build libzip
  python scripts/materialize_c1b_deps.py --verify-only   # hash checks only, no writes
  python scripts/materialize_c1b_deps.py --no-build      # unpack only (no libzip compile)

Exit codes: 0 = ok, 2 = usage/lock error, 3 = distfile missing, 4 = hash mismatch,
            5 = toolchain or build failure.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tarfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_LOCK = os.path.join("third_party", "m12c-c1b", "dependencies.lock.json")


def log(message: str) -> None:
    print(f"[materialize-c1b] {message}", flush=True)


def fail(code: int, message: str) -> "NoReturn":  # type: ignore[name-defined]
    print(f"[materialize-c1b] FAIL: {message}", file=sys.stderr, flush=True)
    sys.exit(code)


def sha256_of(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_lock(lock_path: str) -> dict:
    if not os.path.isfile(lock_path):
        fail(2, f"lock manifest not found: {lock_path}")
    with open(lock_path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def repo_relative(path: str) -> str:
    """Relative-to-repo path when possible, absolute otherwise (temp roots may live on another drive)."""
    absolute = os.path.abspath(path)
    try:
        return os.path.relpath(absolute, REPO_ROOT).replace("\\", "/")
    except ValueError:
        return absolute.replace("\\", "/")


def verify_distfile(path: str, expected_sha: str, label: str) -> None:
    if not os.path.isfile(path):
        fail(
            3,
            f"missing distfile for {label}: {path}\n"
            f"           place the exact-pinned archive there (this tool never downloads); "
            f"see third_party/m12c-c1b/README.md",
        )
    actual = sha256_of(path)
    if actual != expected_sha:
        # check so tampered bytes are silently accepted. Reverted immediately after.
        fail(
            4,
            f"hash mismatch for {label}: {path}\n"
            f"           expected {expected_sha}\n"
            f"           actual   {actual}\n"
            f"           refusing to continue (a tampered or wrong artifact must never be used)",
        )
    log(f"verified {label} sha256={actual}")


def extract_tgz(archive: str, destination: str, stamp_value: str) -> None:
    """Extract deterministically and idempotently.

    A stamp file records which archive digest produced the tree; a re-run with the same
    digest skips the extraction entirely (idempotent layout, and no bulk deletion of a
    previously materialized tree).
    """
    stamp_path = os.path.join(destination, ".materialize-stamp")
    if os.path.isfile(stamp_path):
        with open(stamp_path, "r", encoding="utf-8") as handle:
            if handle.read().strip() == stamp_value:
                log(f"already materialized (stamp matches): {destination}")
                return
    os.makedirs(destination, exist_ok=True)
    with tarfile.open(archive, "r:gz") as tar:
        for member in tar.getmembers():
            name = member.name.replace("\\", "/")
            if name.startswith("/") or ".." in name.split("/"):
                fail(4, f"unsafe entry in {archive}: {member.name}")
        # The archive members are validated just above; extractall() without the
        # Python 3.12+ "filter" argument keeps this runnable on older stdlibs.
        tar.extractall(destination)
    with open(stamp_path, "w", encoding="utf-8") as handle:
        handle.write(stamp_value + "\n")


def read_pdfium_version(pdfium_dir: str) -> dict:
    version = {}
    with open(os.path.join(pdfium_dir, "VERSION"), "r", encoding="utf-8") as handle:
        for line in handle:
            if "=" in line:
                key, value = line.strip().split("=", 1)
                version[key] = value
    return version


def read_args_gn(pdfium_dir: str) -> str:
    with open(os.path.join(pdfium_dir, "args.gn"), "r", encoding="utf-8") as handle:
        return handle.read()


def find_tool(name: str, explicit: str | None) -> str:
    if explicit:
        return explicit
    found = shutil.which(name)
    if found is None:
        fail(5, f"required build tool not found on PATH: {name}")
    return found


def build_libzip(libzip_src: str, libzip_out: str, cmake: str, ninja: str,
                 zlib_include: str, zlib_library: str) -> str:
    build_dir = os.path.join(libzip_out, "build")
    prefix_dir = os.path.join(libzip_out, "prefix")
    os.makedirs(build_dir, exist_ok=True)
    configure = [
        cmake, "-S", libzip_src, "-B", build_dir, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_MAKE_PROGRAM=" + ninja,
        f"-DZLIB_INCLUDE_DIR={zlib_include}",
        f"-DZLIB_LIBRARY={zlib_library}",
        "-DBUILD_SHARED_LIBS=OFF",
        "-DENABLE_OPENSSL=OFF", "-DENABLE_GNUTLS=OFF", "-DENABLE_MBEDTLS=OFF",
        "-DENABLE_WINDOWS_CRYPTO=OFF", "-DENABLE_COMMONCRYPTO=OFF",
        "-DENABLE_BZIP2=OFF", "-DENABLE_LZMA=OFF", "-DENABLE_ZSTD=OFF",
        "-DBUILD_TOOLS=OFF", "-DBUILD_REGRESS=OFF", "-DBUILD_EXAMPLES=OFF",
        "-DBUILD_DOC=OFF",
        f"-DCMAKE_INSTALL_PREFIX={prefix_dir}",
    ]
    result = subprocess.run(configure, capture_output=True, text=True)
    if result.returncode != 0:
        fail(5, "libzip configure failed:\n" + result.stdout[-4000:] + result.stderr[-4000:])
    build = [cmake, "--build", build_dir]
    result = subprocess.run(build, capture_output=True, text=True)
    if result.returncode != 0:
        fail(5, "libzip build failed:\n" + result.stdout[-4000:] + result.stderr[-4000:])
    install = [cmake, "--install", build_dir]
    result = subprocess.run(install, capture_output=True, text=True)
    if result.returncode != 0:
        fail(5, "libzip install failed:\n" + result.stdout[-4000:] + result.stderr[-4000:])
    return os.path.join(prefix_dir, "lib", "libzip.a")


def locate_zlib(include: str | None, library: str | None) -> tuple[str, str, str, str]:
    """Resolves zlib from EXPLICIT inputs only.

    Ambient PATH-first discovery (shutil.which(gcc) -> sysroot) is deliberately gone: it
    picked the first MinGW on PATH and was rejected by the frozen zlib hashes. The actual
    files are re-hashed here and must match the lock manifest exactly.
    """
    if not include or not library:
        fail(5, "explicit zlib inputs are required (no ambient PATH discovery):\n"
                f"           pass --zlib-include <dir> --zlib-library <libz.a>\n"
                f"           (a previously materialized run records them in "
                f"build/deps/m12c-c1b/manifest/materialized.json)")
    header = os.path.abspath(include)
    lib = os.path.abspath(library)
    if not os.path.isfile(header) or not os.path.isfile(lib):
        fail(5, f"pinned zlib inputs missing: {header} / {lib}")
    return header, sha256_of(header), lib, sha256_of(lib)

def main() -> int:
    parser = argparse.ArgumentParser(description="M12-C C1b offline dependency materializer")
    parser.add_argument("--lock", default=DEFAULT_LOCK)
    parser.add_argument("--distfiles", default=None)
    parser.add_argument("--root", default=None)
    parser.add_argument("--verify-only", action="store_true",
                        help="verify the pinned hashes only; never writes anything")
    parser.add_argument("--no-build", action="store_true",
                        help="unpack but do not compile libzip")
    parser.add_argument("--cmake", default=None)
    parser.add_argument("--ninja", default=None)
    parser.add_argument("--zlib-include", default=None,
                        help="directory containing the pinned zlib.h (explicit input; "
                             "ambient PATH discovery is forbidden)")
    parser.add_argument("--zlib-library", default=None,
                        help="path to the pinned libz.a (explicit input)")
    args = parser.parse_args()

    lock = load_lock(args.lock)
    materializer = lock.get("materializer", {})
    distfiles = args.distfiles or os.path.join(
        REPO_ROOT, materializer.get("distfilesDir", "third_party/m12c-c1b/distfiles"))
    root = args.root or os.path.join(
        REPO_ROOT, materializer.get("outputRoot", "build/deps/m12c-c1b"))

    pdfium_pin = lock["dependencies"]["pdfium"]
    libzip_pin = lock["dependencies"]["libzip"]
    zlib_pin = lock["dependencies"]["zlib"]

    log(f"lock={args.lock}")
    log(f"distfiles={distfiles}")
    log(f"root={root}")
    log("network=FORBIDDEN (this tool has no download path)")

    pdfium_archive = os.path.join(distfiles, pdfium_pin["artifact"])
    libzip_archive = os.path.join(distfiles, libzip_pin["archive"])

    # ---- step 1: verify BEFORE any unpack/build --------------------------------
    verify_distfile(pdfium_archive, pdfium_pin["artifactSha256"], "pdfium artifact")
    verify_distfile(libzip_archive, libzip_pin["archiveSha256"], "libzip archive")

    zlib_header, zlib_header_sha, zlib_library, zlib_library_sha = locate_zlib(
        args.zlib_include, args.zlib_library)
    if zlib_header_sha != zlib_pin["headerSha256"]:
        fail(4, f"zlib header hash mismatch: {zlib_header}\n           expected "
                f"{zlib_pin['headerSha256']}\n           actual   {zlib_header_sha}")
    if zlib_library_sha != zlib_pin["librarySha256"]:
        fail(4, f"zlib library hash mismatch: {zlib_library}\n           expected "
                f"{zlib_pin['librarySha256']}\n           actual   {zlib_library_sha}")
    log(f"verified zlib header={zlib_header} library={zlib_library}")

    if args.verify_only:
        log("verify-only: nothing was written")
        return 0

    # ---- step 2: unpack --------------------------------------------------------
    pdfium_dir = os.path.join(root, "pdfium")
    libzip_dir = os.path.join(root, "libzip")
    os.makedirs(root, exist_ok=True)
    extract_tgz(pdfium_archive, pdfium_dir, pdfium_pin["artifactSha256"])
    libzip_src = os.path.join(libzip_dir, "src")
    extract_tgz(libzip_archive, libzip_src, libzip_pin["archiveSha256"])
    # Release tarballs unpack into a single versioned top-level directory.
    if not os.path.isfile(os.path.join(libzip_src, "CMakeLists.txt")):
        children = [name for name in os.listdir(libzip_src)
                    if os.path.isdir(os.path.join(libzip_src, name))]
        if len(children) == 1:
            libzip_src = os.path.join(libzip_src, children[0])
    if not os.path.isfile(os.path.join(libzip_src, "CMakeLists.txt")):
        fail(3, f"libzip sources do not contain CMakeLists.txt: {libzip_src}")

    # ---- step 3: verify the unpacked pdfium identity ---------------------------
    version = read_pdfium_version(pdfium_dir)
    for key, expected in pdfium_pin["expectedVersionFile"].items():
        if version.get(key) != expected:
            fail(4, f"pdfium VERSION.{key} = {version.get(key)!r}, expected {expected!r}")
    args_gn = read_args_gn(pdfium_dir)
    for key, expected in pdfium_pin["expectedArgsGn"].items():
        rendered = "true" if expected is True else ("false" if expected is False else str(expected))
        if expected is True or expected is False:
            needle = f"{key} = {rendered}"
        else:
            needle = f'{key} = "{expected}"'
        if needle not in args_gn:
            fail(4, f"pdfium args.gn is missing expected setting: {needle}")

    dll_path = os.path.join(pdfium_dir, *pdfium_pin["runtimeDllPathInArtifact"].split("/"))
    implib_path = os.path.join(pdfium_dir, *pdfium_pin["importLibraryPathInArtifact"].split("/"))
    dll_sha = sha256_of(dll_path)
    implib_sha = sha256_of(implib_path)
    if dll_sha != pdfium_pin["runtimeDllSha256"]:
        fail(4, f"pdfium.dll hash mismatch: expected {pdfium_pin['runtimeDllSha256']} actual {dll_sha}")
    if implib_sha != pdfium_pin["importLibrarySha256"]:
        fail(4, "pdfium.dll.lib hash mismatch: expected "
                f"{pdfium_pin['importLibrarySha256']} actual {implib_sha}")
    log(f"pdfium.dll sha256={dll_sha}")
    log(f"pdfium.dll.lib sha256={implib_sha}")

    notices_dir = os.path.join(pdfium_dir, *pdfium_pin["thirdPartyNoticesDirInArtifact"].split("/"))
    if not os.path.isdir(notices_dir):
        fail(4, "pdfium third-party notices directory is missing from the artifact")
    notice_count = len(os.listdir(notices_dir))
    log(f"license material retained: LICENSE + {notice_count} third-party notices")

    # ---- step 4: build libzip (skippable) --------------------------------------
    libzip_static = None
    if not args.no_build:
        cmake = find_tool("cmake", args.cmake)
        ninja = find_tool("ninja", args.ninja)
        libzip_static = build_libzip(libzip_src, libzip_dir, cmake, ninja,
                                     os.path.dirname(zlib_header), zlib_library)
        log(f"libzip.a sha256={sha256_of(libzip_static)}")

    # ---- step 5: write the materialized manifest -------------------------------
    manifest_dir = os.path.join(root, "manifest")
    os.makedirs(manifest_dir, exist_ok=True)
    manifest = {
        "schemaVersion": 1,
        "scope": lock.get("scope"),
        "lock": repo_relative(args.lock),
        "pdfium": {
            "version": pdfium_pin["version"],
            "upstreamCommit": pdfium_pin["upstreamCommit"],
            "dir": repo_relative(pdfium_dir),
            "includeDir": repo_relative(
                os.path.join(pdfium_dir, pdfium_pin["publicHeadersDirInArtifact"])),
            "runtimeDll": repo_relative(dll_path),
            "runtimeDllSha256": dll_sha,
            "importLibrary": repo_relative(implib_path),
            "importLibrarySha256": implib_sha,
            "licenseFile": repo_relative(
                os.path.join(pdfium_dir, pdfium_pin["licenseFileInArtifact"])),
            "thirdPartyNoticesDir": repo_relative(notices_dir),
            "thirdPartyNoticeCount": notice_count,
        },
        "libzip": {
            "version": libzip_pin["version"],
            "dir": repo_relative(libzip_dir),
            "sourceDir": repo_relative(libzip_src),
            "staticLibrary": None if libzip_static is None
            else repo_relative(libzip_static),
            "staticLibrarySha256": None if libzip_static is None else sha256_of(libzip_static),
            "includeDir": None if libzip_static is None
            else repo_relative(os.path.join(libzip_dir, "prefix", "include")),
        },
        "zlib": {
            "version": zlib_pin["version"],
            "header": zlib_header,
            "headerSha256": zlib_header_sha,
            "library": zlib_library,
            "librarySha256": zlib_library_sha,
            "machinePathsInTrackedFiles": False,
        },
    }
    manifest_path = os.path.join(manifest_dir, "materialized.json")
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log(f"wrote {repo_relative(manifest_path)}")
    log("materialization complete (offline)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
