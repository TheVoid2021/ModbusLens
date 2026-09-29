# M12 Windows deployment architecture (Session H): deployment-startup gate.
#
# The gate consumes ONLY the self-contained candidate tree
# (${CMAKE_BINARY_DIR}/candidate/ModbusLens) — never the raw build output:
#
#   1. (unless NO_GENERATE) regenerate the candidate FROM SCRATCH via
#      cmake/modbuslens_generate_candidate.cmake — generation failure fails
#      the gate;
#   2. verify candidate-manifest.json exists and covers every staged file;
#   3. verify the required runtime closure and the manifest spot-hashes
#      (compiler runtime must equal the active toolchain);
#   4. launch candidate/modbuslens.exe --qml-smoke-test under a sanitized
#      environment (PATH = candidate root + Windows system dirs ONLY; all
#      Qt/QML override variables cleared) and require exit 0 +
#      "SMOKE IDENTITY PASS" + QT_DEBUG_PLUGINS proof that qwindows.dll was
#      loaded from the candidate tree.
#
# usage:
#   cmake -DCANDIDATE_DIR=<candidate/ModbusLens> -DEXE_NAME=modbuslens.exe
#         -DCOMPILER_BIN=<active MinGW bin> -DQT_PREFIX=<active Qt prefix>
#         [-DNO_GENERATE=1]
#         -P tests/deployment_startup_check.cmake

if(NOT CANDIDATE_DIR OR NOT EXE_NAME OR NOT COMPILER_BIN OR NOT QT_PREFIX)
    message(FATAL_ERROR "deployment_startup_check: required inputs missing")
endif()

set(_cat "${CANDIDATE_DIR}")
set(_exe "${_cat}/${EXE_NAME}")
set(_system "$ENV{SystemRoot}/System32")

# ---- 1. regenerate the candidate from scratch (default) --------------------
if(NOT NO_GENERATE)
    if(NOT APP_EXE OR NOT PDFIUM_DLL OR NOT QML_MODULE_DIR)
        message(FATAL_ERROR
            "deployment_startup_check: APP_EXE / PDFIUM_DLL required for generation")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DCANDIDATE_DIR=${_cat}
            -DCOMPILER_BIN=${COMPILER_BIN}
            -DQT_PREFIX=${QT_PREFIX}
            -DAPP_EXE=${APP_EXE}
            -DPDFIUM_DLL=${PDFIUM_DLL}
            -DQML_MODULE_DIR=${QML_MODULE_DIR}
            -P "${CMAKE_CURRENT_LIST_DIR}/../cmake/modbuslens_generate_candidate.cmake"
        RESULT_VARIABLE _gen_result
        OUTPUT_VARIABLE _gen_out
        ERROR_VARIABLE _gen_err
    )
    if(NOT _gen_result EQUAL 0)
        message(FATAL_ERROR
            "deployment_startup_check: candidate generation FAILED (${_gen_result})\n"
            "  generator output: ${_gen_out}${_gen_err}")
    endif()
endif()

# ---- 2. manifest present ----------------------------------------------------
if(NOT EXISTS "${_cat}/candidate-manifest.json")
    message(FATAL_ERROR
        "deployment_startup_check: candidate-manifest.json missing from ${_cat}")
endif()

# ---- 3. required runtime closure + manifest spot-hash verification ----------
set(_required
    modbuslens.exe
    pdfium.dll
    libstdc++-6.dll
    libgcc_s_seh-1.dll
    libwinpthread-1.dll
    Qt6Core.dll
    Qt6Gui.dll
    Qt6Qml.dll
    Qt6Quick.dll
    Qt6QuickControls2.dll
    platforms/qwindows.dll
    platforms/qoffscreen.dll
    qml/QtQuick/Controls/qmldir
    qt.conf
    candidate-manifest.json)
foreach(_rel ${_required})
    if(NOT EXISTS "${_cat}/${_rel}")
        message(FATAL_ERROR
            "deployment_startup_check: required candidate file missing: ${_rel}")
    endif()
endforeach()

# The compiler runtime must be the ACTIVE toolchain runtime, byte for byte.
foreach(_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
    file(SHA256 "${_cat}/${_dll}" _staged_sha)
    file(SHA256 "${COMPILER_BIN}/${_dll}" _canonical_sha)
    if(NOT _staged_sha STREQUAL _canonical_sha)
        message(FATAL_ERROR
            "deployment_startup_check: staged ${_dll} is NOT the active toolchain runtime\n"
            "  staged    ${_staged_sha}\n"
            "  canonical ${_canonical_sha}")
    endif()
endforeach()

# The Qt runtime must match the active kit (spot check on the core DLL).
file(SHA256 "${_cat}/Qt6Core.dll" _qt_staged)
file(SHA256 "${QT_PREFIX}/bin/Qt6Core.dll" _qt_canonical)
if(NOT _qt_staged STREQUAL _qt_canonical)
    message(FATAL_ERROR
        "deployment_startup_check: staged Qt6Core.dll is NOT the active kit runtime\n"
        "  staged    ${_qt_staged}\n"
        "  kit       ${_qt_canonical}")
endif()

# ---- 4. sanitized Explorer-style launch of the candidate --------------------
# PATH contains ONLY the candidate root and the Windows system directories.
# Qt/QML override variables are cleared so no inherited environment can
# redirect plugin or import resolution. execute_process has no per-call
# ENVIRONMENT argument, so the sanitized values are installed into this
# process environment first (children inherit them).
set(ENV{PATH} "${_cat};${_system};$ENV{SystemRoot}")
set(ENV{QT_PLUGIN_PATH} "")
set(ENV{QT_QPA_PLATFORM_PLUGIN_PATH} "")
set(ENV{QML_IMPORT_PATH} "")
set(ENV{QML2_IMPORT_PATH} "")
set(ENV{QT_QPA_PLATFORM} "windows")
set(ENV{QT_ASSUME_STDERR_HAS_CONSOLE} "1")
set(ENV{QT_DEBUG_PLUGINS} "1")
set(ENV{QT_FORCE_STDERR_LOGGING} "1")
execute_process(
    COMMAND "${_exe}" --qml-smoke-test
    WORKING_DIRECTORY "${_cat}"
    RESULT_VARIABLE _result
    TIMEOUT 300
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "deployment_startup_check: candidate launch FAILED\n"
        "  exit/result: ${_result}\n"
        "  stderr tail: ${_err}")
endif()
if(NOT _err MATCHES "SMOKE IDENTITY PASS")
    message(FATAL_ERROR
        "deployment_startup_check: the smoke marker is missing\n"
        "  stderr tail: ${_err}")
endif()

# The plugin debug log must prove the platform plugin was loaded from the
# CANDIDATE tree (not from any developer installation).
if(NOT _err MATCHES "${_cat}/platforms/qwindows\\.dll")
    message(FATAL_ERROR
        "deployment_startup_check: the log does not prove qwindows.dll was "
        "loaded from the candidate tree\n"
        "  stderr tail: ${_err}")
endif()

message(STATUS
    "deployment_startup_check: candidate ${_cat} — manifest verified, "
    "sanitized launch PASSED (exit 0, SMOKE IDENTITY PASS, qwindows from candidate)")
