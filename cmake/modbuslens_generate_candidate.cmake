# M12 Windows deployment architecture (Session H): self-contained candidate
# generator.
#
# Defines the permanent rule: RAW BUILD OUTPUT != DEPLOYABLE CANDIDATE.
# The only deployable artifact root for the current build configuration is
# ${CMAKE_BINARY_DIR}/candidate/ModbusLens, generated FROM SCRATCH on every
# invocation (the candidate root is owned and emptied by this script; candidate
# files are never inputs; no historical deploy/package tree is ever a source).
#
# Runtime sources (all identity-bound, never PATH searches):
#   * the current application target        (APP_EXE)
#   * the frozen PDFium materialization     (PDFIUM_DLL)
#   * the active MinGW toolchain            (COMPILER_BIN)
#   * the active Qt installation            (QT_PREFIX, from Qt6_DIR)
#
# usage:
#   cmake -DCANDIDATE_DIR=... -DAPP_EXE=... -DPDFIUM_DLL=...
#         -DCOMPILER_BIN=... -DQT_PREFIX=...
#         -P cmake/modbuslens_generate_candidate.cmake

if(NOT CANDIDATE_DIR OR NOT APP_EXE OR NOT PDFIUM_DLL OR NOT COMPILER_BIN
   OR NOT QT_PREFIX OR NOT QML_MODULE_DIR)
    message(FATAL_ERROR "generate_candidate: required inputs missing")
endif()

set(_cat "${CANDIDATE_DIR}")

# ---- 1. the candidate root is emptied: generation always starts from zero ---
if(EXISTS "${_cat}")
    file(REMOVE_RECURSE "${_cat}")
endif()
file(MAKE_DIRECTORY "${_cat}")

# ---- 2. application + frozen pdfium -----------------------------------------
file(COPY_FILE "${APP_EXE}" "${_cat}/modbuslens.exe")
file(COPY_FILE "${PDFIUM_DLL}" "${_cat}/pdfium.dll")

# ---- 3. compiler runtime closure (active toolchain, never PATH) -------------
foreach(_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
    if(NOT EXISTS "${COMPILER_BIN}/${_dll}")
        message(FATAL_ERROR "generate_candidate: missing toolchain runtime ${_dll}")
    endif()
    file(COPY_FILE "${COMPILER_BIN}/${_dll}" "${_cat}/${_dll}")
endforeach()

# ---- 4. Qt runtime DLL closure (active kit bin) -----------------------------
set(_qt_dlls
    Qt6Core.dll Qt6Gui.dll Qt6Qml.dll Qt6QmlMeta.dll Qt6QmlModels.dll
    Qt6QmlWorkerScript.dll Qt6OpenGL.dll Qt6Quick.dll Qt6QuickControls2.dll
    Qt6QuickTemplates2.dll Qt6Network.dll Qt6SerialPort.dll
    Qt6QuickLayouts.dll Qt6QuickShapes.dll Qt6QuickEffects.dll
    Qt6QuickControls2Impl.dll Qt6QuickControls2Basic.dll
    Qt6QuickControls2BasicStyleImpl.dll Qt6QuickControls2Fusion.dll
    Qt6QuickControls2FusionStyleImpl.dll Qt6QuickControls2Imagine.dll
    Qt6QuickControls2ImagineStyleImpl.dll Qt6QuickControls2Material.dll
    Qt6QuickControls2MaterialStyleImpl.dll Qt6QuickControls2Universal.dll
    Qt6QuickControls2UniversalStyleImpl.dll Qt6QuickControls2WindowsStyleImpl.dll
    Qt6QuickDialogs2.dll Qt6QuickDialogs2QuickImpl.dll Qt6QuickDialogs2Utils.dll
    Qt6Svg.dll Qt6LabsFolderListModel.dll D3Dcompiler_47.dll)
foreach(_dll ${_qt_dlls})
    if(NOT EXISTS "${QT_PREFIX}/bin/${_dll}")
        message(FATAL_ERROR "generate_candidate: missing Qt runtime ${_dll}")
    endif()
    file(COPY_FILE "${QT_PREFIX}/bin/${_dll}" "${_cat}/${_dll}")
endforeach()

# ---- 5. qt.conf (Scheme A: application-relative single plugin scheme) -------
file(WRITE "${_cat}/qt.conf" "[Paths]\nPrefix=.\nQml2Imports=qml\n")

# ---- 6. platform plugins ----------------------------------------------------
file(MAKE_DIRECTORY "${_cat}/platforms")
foreach(_plat qwindows.dll qoffscreen.dll)
    if(NOT EXISTS "${QT_PREFIX}/plugins/platforms/${_plat}")
        message(FATAL_ERROR "generate_candidate: missing platform plugin ${_plat}")
    endif()
    file(COPY_FILE "${QT_PREFIX}/plugins/platforms/${_plat}"
                   "${_cat}/platforms/${_plat}")
endforeach()

# ---- 7. plugin families (app-root-relative, Scheme A) -----------------------
foreach(_family imageformats iconengines tls networkinformation)
    if(EXISTS "${QT_PREFIX}/plugins/${_family}")
        file(COPY "${QT_PREFIX}/plugins/${_family}" DESTINATION "${_cat}")
    endif()
endforeach()

# ---- 8. the application's own QML module (current target build output) -----
# qt_add_qml_module materializes the ModbusLens module (qmldir, qmltypes,
# sources, assets) under the binary dir; the QML engine resolves
# `import ModbusLens` from the import root, so it must sit at
# candidate/qml/ModbusLens exactly as windeployqt historically deployed it.
file(COPY "${QML_MODULE_DIR}" DESTINATION "${_cat}/qml")

# ---- 9. QML import closure (current kit) ------------------------------------
foreach(_module Qt QtQml QtQuick)
    if(NOT IS_DIRECTORY "${QT_PREFIX}/qml/${_module}")
        message(FATAL_ERROR "generate_candidate: missing QML module ${_module}")
    endif()
    file(COPY "${QT_PREFIX}/qml/${_module}" DESTINATION "${_cat}/qml")
endforeach()

# ---- 10. manifest (build-only; every staged file, SHA-256) -------------------
set(_entries)
file(GLOB_RECURSE _files LIST_DIRECTORIES false "${_cat}/*")
list(SORT _files)
foreach(_f ${_files})
    file(RELATIVE_PATH _rel "${_cat}" "${_f}")
    file(SHA256 "${_f}" _sha)
    get_filename_component(_name "${_f}" NAME)
    set(_cat_name "other")
    if(_name STREQUAL "modbuslens.exe")
        set(_cat_name "application")
    elseif(_name STREQUAL "pdfium.dll")
        set(_cat_name "frozen-pdfium")
    elseif(_name MATCHES "^(libstdc\+\+-6|libgcc_s_seh-1|libwinpthread-1)\\.dll$")
        set(_cat_name "compiler-runtime")
    elseif(_name MATCHES "^Qt6.*\\.dll$" OR _name STREQUAL "D3Dcompiler_47.dll")
        set(_cat_name "qt-runtime")
    elseif(_rel MATCHES "^platforms/")
        set(_cat_name "qt-platform-plugin")
    elseif(_rel MATCHES "^(imageformats|iconengines|tls|networkinformation)/")
        set(_cat_name "qt-plugin")
    elseif(_rel MATCHES "^qml/")
        set(_cat_name "qml-import")
    elseif(_name STREQUAL "qt.conf")
        set(_cat_name "qt-conf")
    endif()
    string(APPEND _entries
        "  { \"path\": \"${_rel}\", \"category\": \"${_cat_name}\", \"sha256\": \"${_sha}\" },\n")
endforeach()
string(APPEND _entries "__END__")
string(REPLACE ",\n__END__" "\n" _entries "${_entries}")
file(WRITE "${_cat}/candidate-manifest.json"
    "{\n  \"generated-by\": \"modbuslens_generate_candidate.cmake\",\n  \"files\": [\n${_entries}  ]\n}\n")

list(LENGTH _files _count)
message(STATUS "generate_candidate: candidate ready at ${_cat} (${_count} files, manifest written)")
