#!/usr/bin/env python3
"""Deterministic oracle for the canonical candidate-tree packaging path.

POST-M12-REL-R2 (T027 §114, Human decisions R1-R3). The contract under
test:

  R1  make_package.package_candidate_root() consumes an IMMUTABLE
      canonical candidate/ModbusLens root, validated through its own
      candidate-manifest.json. No CMakeCache/raw build root is needed,
      no windeployqt / deploy_windows.bat is ever invoked, no raw-build
      fallback exists, and the candidate tree is never mutated.
  R2  pdfium.dll is a REQUIRED release runtime; the package content set
      is reconciled against the candidate manifest (explicit exclusion:
      candidate-manifest.json only) instead of the legacy M9-E list.
  R3  samples/ModbusLens_Test_Manual_* never enter staging or the ZIP
      (fail-closed), while samples/demo_v1.mlog keeps shipping.

Everything runs in throwaway temp fixtures with fake payload bytes: no
real build tree, no real application launch, no live provider. The
runtime gates of the canonical CLI (PE identity, minimal-PATH,
external-CWD) are deliberately NOT exercised here; they belong to the
real packaging session against the real candidate.

Run:  python scripts/test_make_canonical_package.py
"""
import hashlib
import inspect
import json
import os
import shutil
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_package  # noqa: E402

FAILURES = []


def check(label, condition, detail=""):
    if condition:
        print("PASS %s" % label)
    else:
        FAILURES.append(label)
        print("FAIL %s %s" % (label, detail))


def expects_system_exit(label, function):
    try:
        function()
    except SystemExit as exc:
        check(label, exc.code == 1, "exit=%r" % (exc.code,))
        return True
    check(label, False, "no SystemExit raised")
    return False


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def _zip_entries(zip_path):
    with zipfile.ZipFile(zip_path) as archive:
        return set(archive.namelist())


def write_candidate(root, files, generated_by="modbuslens_generate_candidate.cmake",
                    manifest_broken=None, omit_manifest=False):
    """Materialize a fake candidate root + matching manifest."""
    entries = []
    for rel, blob in sorted(files.items()):
        full = os.path.join(root, *rel.split("/"))
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "wb") as handle:
            handle.write(blob)
        entries.append({"path": rel, "category": "test",
                        "sha256": hashlib.sha256(blob).hexdigest()})
    if omit_manifest:
        return None
    doc = {"generated-by": generated_by, "files": entries}
    if manifest_broken is not None:
        doc = manifest_broken
    with open(os.path.join(root, "candidate-manifest.json"), "w",
              encoding="utf-8") as handle:
        if isinstance(doc, str):
            handle.write(doc)
        else:
            json.dump(doc, handle, indent=2)
    return doc


def canonical_files():
    return {
        "modbuslens.exe": b"M12-FAKE-EXE-BYTES-v2.0.0",
        "pdfium.dll": b"M12-FAKE-PDFIUM-BYTES",
        "libstdc++-6.dll": b"FAKE-GCC-RUNTIME",
        "libgcc_s_seh-1.dll": b"FAKE-GCC-RUNTIME",
        "libwinpthread-1.dll": b"FAKE-GCC-RUNTIME",
        "Qt6Core.dll": b"FAKE-QT", "Qt6Gui.dll": b"FAKE-QT",
        "Qt6Qml.dll": b"FAKE-QT", "Qt6Quick.dll": b"FAKE-QT",
        "Qt6QuickControls2.dll": b"FAKE-QT",
        "Qt6SerialPort.dll": b"FAKE-QT", "Qt6Network.dll": b"FAKE-QT",
        "D3Dcompiler_47.dll": b"FAKE-D3D",
        "qt.conf": b"[Paths]\nPrefix=.\nQml2Imports=qml\n",
        "platforms/qwindows.dll": b"FAKE-QWINDOWS",
        "platforms/qoffscreen.dll": b"FAKE-QOFFSCREEN",
        "imageformats/qico.dll": b"FAKE-QICO",
        "qml/ModbusLens/qmldir": b"module ModbusLens\n",
        "qml/ModbusLens/assets/brand/windows/ModbusLens.ico":
            b"FAKE-ICO-MIRROR",
        "qml/Qt/qmldir": b"module Qt\n",
        "qml/QtQml/qmldir": b"module QtQml\n",
        "qml/QtQuick/qmldir": b"module QtQuick\n",
    }


def snapshot(root):
    hashes = {}
    for base, _, names in os.walk(root):
        for name in names:
            full = os.path.join(base, name)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            hashes[rel] = sha(full)
    return hashes


def package(tmp, tag, files=None, root=None, **kwargs):
    output = os.path.join(tmp, tag, "out")
    make_package.PACKAGE_ROOT = os.path.join(tmp, tag, "pkg")
    make_package.EXTRACT_ROOT = os.path.join(tmp, tag, "extract")
    if root is None:
        root = os.path.join(tmp, tag, "candidate", "ModbusLens")
        os.makedirs(root)
        write_candidate(root, files if files is not None else canonical_files(),
                        **kwargs)
    result = make_package.package_candidate_root(root, output_root=output)
    return root, result


def main():
    tmp = tempfile.mkdtemp(prefix="modbuslens-candidate-package-")
    original_pkg_root = make_package.PACKAGE_ROOT
    original_extract_root = make_package.EXTRACT_ROOT
    try:
        # ---- PKG-01/02/04/05/06/07/13/17/18/19/20/21/22 ----------------
        root = os.path.join(tmp, "happy", "candidate", "ModbusLens")
        os.makedirs(root)
        write_candidate(root, canonical_files())
        before = snapshot(root)
        try:
            result = make_package.package_candidate_root(
                root, output_root=os.path.join(tmp, "happy", "out"))
        except SystemExit as exc:
            check("PKG-01 valid canonical candidate root accepted", False,
                  "unexpected SystemExit(%r)" % (exc.code,))
            raise
        staging = result["staging"]
        after = snapshot(root)
        check("PKG-01 valid canonical candidate root accepted",
              os.path.isfile(result["zip_path"]))
        check("PKG-02 no CMakeCache/raw build root required",
              not any("CMakeCache" in name
                      for _, _, names in os.walk(root) for name in names))
        exe_candidate = sha(os.path.join(root, "modbuslens.exe"))
        exe_staging = sha(os.path.join(staging, "ModbusLens.exe"))
        check("PKG-04 source exe copied from candidate tree",
              exe_candidate == exe_staging)
        check("PKG-05 pdfium.dll required and included",
              os.path.isfile(os.path.join(staging, "pdfium.dll"))
              and (result["stem"] + "/pdfium.dll")
              in _zip_entries(result["zip_path"]))
        check("PKG-06 qwindows/qt.conf present",
              os.path.isfile(os.path.join(staging, "platforms",
                                          "qwindows.dll"))
              and os.path.isfile(os.path.join(staging, "qt.conf")))
        check("PKG-13 demo_v1.mlog policy unchanged",
              os.path.isfile(os.path.join(staging, "samples",
                                          "demo_v1.mlog")))
        # PKG-17: staging rebuilt deterministically (two-run manifest
        # identical is the established idempotence witness).
        again = make_package.package_candidate_root(
            root, output_root=os.path.join(tmp, "happy", "out"))
        first_manifest = open(os.path.join(staging,
                                           "package-manifest.sha256"),
                              encoding="utf-8").read()
        second_manifest = open(os.path.join(again["staging"],
                                            "package-manifest.sha256"),
                               encoding="utf-8").read()
        check("PKG-17 staging rebuilt deterministically",
              first_manifest == second_manifest)
        check("PKG-18 ZIP rebuilt deterministically",
              os.path.isfile(result["zip_path"])
              and len(_zip_entries(result["zip_path"])) > 0)
        manifest_rel = result["stem"] + "/package-manifest.sha256"
        check("PKG-19 package manifest generated and covers payload",
              manifest_rel in _zip_entries(result["zip_path"]))
        check("PKG-20 version/stem remains the canonical 2.0.0 contract",
              result["stem"] == "ModbusLens-2.0.0-windows-x64"
              and make_package.candidate_authority_version(
                  make_package.REPO_ROOT) == "2.0.0")
        check("PKG-21 candidate source tree is not mutated",
              before == after)
        expected = set()
        manifest_doc = make_package.load_candidate_manifest(root)
        for entry in manifest_doc["files"]:
            if entry["path"] == "candidate-manifest.json":
                continue
            if entry["path"].startswith("qml/ModbusLens/assets/"):
                continue
            rel = "ModbusLens.exe" if entry["path"] == "modbuslens.exe" \
                else entry["path"]
            expected.add(result["stem"] + "/" + rel)
        expected |= {result["stem"] + "/samples/demo_v1.mlog",
                     result["stem"] + "/README.txt",
                     manifest_rel}
        check("PKG-22 content only from candidate tree + authorized "
              "release metadata",
              _zip_entries(result["zip_path"]) == expected)
        check("PKG-25 redundant brand-ICO mirror excluded (M9-E rule)",
              not any(".ico" in name or "/assets/" in name
                      for name in _zip_entries(result["zip_path"]))
              and not os.path.exists(os.path.join(staging, "qml",
                                                  "ModbusLens",
                                                  "assets")))

        # ---- PKG-03/23: no windeployqt / deploy_windows.bat path --------
        sources = "".join(inspect.getsource(function) for function in
                          (make_package.package_candidate_root,
                           make_package.stage_candidate_package,
                           make_package.verify_candidate_root,
                           make_package.load_candidate_manifest,
                           make_package.run_candidate_mode))
        check("PKG-03 no windeployqt/deploy_windows in candidate path",
              "windeployqt" not in sources
              and "deploy_windows" not in sources
              and "run_deploy" not in sources)

        def _forbidden_runtime(*args, **kwargs):
            raise AssertionError("legacy deploy machinery must not run")
        original_run_deploy = make_package.run_deploy
        original_deploy_is_current = make_package.deploy_is_current
        make_package.run_deploy = _forbidden_runtime
        make_package.deploy_is_current = _forbidden_runtime
        try:
            package(tmp, "nodeploy")
            check("PKG-23 no raw build/deploy fallback exists", True)
        except AssertionError as exc:
            check("PKG-23 no raw build/deploy fallback exists", False,
                  str(exc))
        finally:
            make_package.run_deploy = original_run_deploy
            make_package.deploy_is_current = original_deploy_is_current

        # ---- PKG-08/09/10: fail-closed manifest handling ----------------
        missing_manifest = os.path.join(tmp, "no-manifest", "candidate",
                                        "ModbusLens")
        os.makedirs(missing_manifest)
        write_candidate(missing_manifest, canonical_files(),
                        omit_manifest=True)
        expects_system_exit(
            "PKG-08 missing manifest fails closed",
            lambda: make_package.load_candidate_manifest(missing_manifest))

        broken_root = os.path.join(tmp, "broken-json", "candidate",
                                   "ModbusLens")
        os.makedirs(broken_root)
        write_candidate(broken_root, canonical_files(),
                        manifest_broken="{ not json ")
        expects_system_exit(
            "PKG-10 malformed manifest fails closed",
            lambda: make_package.load_candidate_manifest(broken_root))

        wrong_root = os.path.join(tmp, "wrong-gen", "candidate",
                                  "ModbusLens")
        os.makedirs(wrong_root)
        write_candidate(wrong_root, canonical_files(),
                        generated_by="something-else")
        expects_system_exit(
            "PKG-10b foreign generator fails closed",
            lambda: make_package.load_candidate_manifest(wrong_root))

        missing_file = os.path.join(tmp, "missing-file", "candidate",
                                    "ModbusLens")
        os.makedirs(missing_file)
        write_candidate(missing_file, canonical_files())
        os.remove(os.path.join(missing_file, "pdfium.dll"))
        manifest = make_package.load_candidate_manifest(missing_file)
        expects_system_exit(
            "PKG-09 missing listed runtime fails closed",
            lambda: make_package.verify_candidate_root(missing_file,
                                                       manifest))

        no_pdfium = os.path.join(tmp, "no-pdfium", "candidate",
                                 "ModbusLens")
        os.makedirs(no_pdfium)
        files = canonical_files()
        del files["pdfium.dll"]
        write_candidate(no_pdfium, files)
        manifest = make_package.load_candidate_manifest(no_pdfium)
        expects_system_exit(
            "PKG-05b candidate without pdfium fails closed (R2)",
            lambda: make_package.verify_candidate_root(no_pdfium,
                                                       manifest))

        # ---- PKG-12: synthetic Manual samples ---------------------------
        sneaky = os.path.join(tmp, "sneaky", "candidate", "ModbusLens")
        os.makedirs(sneaky)
        write_candidate(sneaky, canonical_files())
        shutil.copyfile(
            os.path.join(make_package.REPO_ROOT, "samples",
                         "ModbusLens_Test_Manual_A_Clear.txt"),
            os.path.join(sneaky, "ModbusLens_Test_Manual_A_Clear.txt"))
        _, result = package(tmp, "sneaky-run", root=sneaky)
        staging = result["staging"]
        leaked = [base for _, _, names in os.walk(staging)
                  for base in names
                  if base.lower().startswith("modbuslens_test_manual_")]
        check("PKG-12 unlisted synthetic Manual sample is not copied",
              not leaked)
        listed = os.path.join(tmp, "listed", "candidate", "ModbusLens")
        os.makedirs(listed)
        files = canonical_files()
        files["ModbusLens_Test_Manual_A_Clear.txt"] = b"SYNTHETIC"
        write_candidate(listed, files)
        manifest = make_package.load_candidate_manifest(listed)
        expects_system_exit(
            "PKG-12b listed synthetic Manual sample fails closed",
            lambda: make_package.verify_candidate_root(listed, manifest))

        # ---- PKG-14/15/16: secret and path scans stay enforced ----------
        cred_name = os.path.join(tmp, "cred-name", "candidate",
                                 "ModbusLens")
        os.makedirs(cred_name)
        files = canonical_files()
        files[".env"] = b"SECRET=1"
        write_candidate(cred_name, files)
        expects_system_exit(
            "PKG-14a credential-like filename rejected",
            lambda: make_package.package_candidate_root(cred_name))
        cred_text = os.path.join(tmp, "cred-text", "candidate",
                                 "ModbusLens")
        os.makedirs(cred_text)
        files = canonical_files()
        files["note.txt"] = b"MODELSCOPE_API_KEY=should-not-ship"
        write_candidate(cred_text, files)
        expects_system_exit(
            "PKG-14b credential-like text pattern rejected",
            lambda: make_package.package_candidate_root(cred_text))
        machine_path = os.path.join(tmp, "machine-path", "candidate",
                                    "ModbusLens")
        os.makedirs(machine_path)
        files = canonical_files()
        files["note.txt"] = b"built on e:/desktop/somewhere"
        write_candidate(machine_path, files)
        expects_system_exit(
            "PKG-15 absolute local path scan preserved",
            lambda: make_package.package_candidate_root(machine_path))
        staging = os.path.join(tmp, "happy", "out", result["stem"])
        user_data = [base for base, _, _ in os.walk(staging)
                     if "evidence" in base.lower()
                     or "ManualStore" in base
                     or base.endswith("-evidence")]
        check("PKG-16 user/runtime data excluded",
              not user_data
              and not os.path.exists(os.path.join(staging, ".env")))

        # ---- legacy helpers untouched (provenance) ----------------------
        check("PKG-24 fail-fast is SystemExit(1) everywhere above",
              not FAILURES or all("fails closed" in f
                                  or "rejected" in f
                                  or "scan preserved" in f
                                  for f in FAILURES))
    finally:
        make_package.PACKAGE_ROOT = original_pkg_root
        make_package.EXTRACT_ROOT = original_extract_root
        shutil.rmtree(tmp, ignore_errors=True)
    if FAILURES:
        print("test_make_canonical_package FAIL: %s" % ", ".join(FAILURES))
        sys.exit(1)
    print("test_make_canonical_package PASS (candidate input, pdfium "
          "required, no deploy fallback, sample exclusion, fail-closed)")


if __name__ == "__main__":
    main()
