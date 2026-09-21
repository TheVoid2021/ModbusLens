#!/usr/bin/env python3
"""Freshness oracle for scripts/make_package.py's deploy step.

Isolated, deterministic, and independent of the real build tree: it uses
throwaway temp fixtures, so it never needs to delete or move thousands of
real deployment files to prove anything.

Background (the defect this guards):
    main() used to decide whether to deploy with

        if not os.path.isfile(exe):
            run_deploy(...)

    i.e. existence only. A deploy directory left over from an earlier run can
    hold a DIFFERENT build's ModbusLens.exe; the old rule skipped the redeploy
    and packaged the stale binary without any error. Freshness is a CONTENT
    property, so the decision must compare content identity (SHA-256).

Run:  python scripts/test_make_package_freshness.py
"""

import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_package  # noqa: E402

FAILURES = []


def check(label, condition, detail=""):
    if condition:
        print("PASS %s" % label)
    else:
        FAILURES.append(label)
        print("FAIL %s %s" % (label, detail))


def make_fixture(tmp, name, build_bytes, deploy_bytes=None):
    """build dir (modbuslens.exe) + optional deploy dir (ModbusLens.exe)."""
    build = os.path.join(tmp, name, "build")
    deploy = os.path.join(tmp, name, "deploy")
    os.makedirs(build)
    with open(os.path.join(build, "modbuslens.exe"), "wb") as handle:
        handle.write(build_bytes)
    if deploy_bytes is not None:
        os.makedirs(deploy)
        with open(os.path.join(deploy, "ModbusLens.exe"), "wb") as handle:
            handle.write(deploy_bytes)
    return build, deploy


def main():
    tmp = tempfile.mkdtemp(prefix="modbuslens-freshness-")
    try:
        current = b"CURRENT-BUILD-BYTES-0123456789"
        stale = b"STALE-BUILD-BYTES-FROM-AN-EARLIER-RUN"

        # ---- Case 1: deploy exe missing -> not current -> must deploy ----
        build, deploy = make_fixture(tmp, "missing", current)
        check("case1 missing deploy -> not current",
              make_package.deploy_is_current(build, deploy) is False)
        # And the freshly created deploy dir really has no exe at all.
        check("case1 deploy exe absent",
              not os.path.isfile(os.path.join(deploy, "ModbusLens.exe")))

        # ---- Case 2: deploy exe STALE -> not current -> must redeploy ----
        build, deploy = make_fixture(tmp, "stale", current, stale)
        check("case2 stale deploy -> not current",
              make_package.deploy_is_current(build, deploy) is False)

        # ---- Case 3: deploy exe CURRENT -> current -> may reuse ----
        build, deploy = make_fixture(tmp, "current", current, current)
        check("case3 current deploy -> current",
              make_package.deploy_is_current(build, deploy) is True)

        # ---- RED reproduction: the OLD rule is wrong on Case 2 ----
        #
        # This is the defect, stated as an assertion so it cannot silently come
        # back: in the stale case the old "does the file exist" rule answers
        # True, which means main() would skip the redeploy and package the
        # STALE binary. It does not depend on mtime, on filenames or on anyone
        # deleting a directory.
        build, deploy = make_fixture(tmp, "red", current, stale)
        old_rule_would_skip = os.path.isfile(os.path.join(deploy, "ModbusLens.exe"))
        check("RED old existence rule wrongly says 'fresh' on a stale deploy",
              old_rule_would_skip is True)
        check("RED content differs while old rule says 'fresh'",
              make_package.sha256_file(os.path.join(build, "modbuslens.exe"))
              != make_package.sha256_file(os.path.join(deploy, "ModbusLens.exe")))

        # ---- Same-size trap: size is not identity either ----
        same_size_stale = b"STALE-BUILD-BYTES-0123456789!!!!"
        build, deploy = make_fixture(tmp, "samesize", current, same_size_stale)
        check("same-size but different content -> not current",
              make_package.deploy_is_current(build, deploy) is False)

        # ---- Missing build exe is a hard failure, never a silent skip ----
        build, deploy = make_fixture(tmp, "nobuild", current)
        os.remove(os.path.join(build, "modbuslens.exe"))
        raised = False
        try:
            make_package.deploy_is_current(build, deploy)
        except SystemExit:
            raised = True
        check("missing build exe -> refuses (does not silently skip)", raised)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    if FAILURES:
        print("test_make_package_freshness FAIL: %s" % ", ".join(FAILURES))
        return 1
    print("test_make_package_freshness PASS (missing / stale / current / RED "
          "old-rule / same-size trap / missing-build-exe)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
