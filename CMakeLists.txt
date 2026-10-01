cmake_minimum_required(VERSION 3.21)

project(ModbusLens
    VERSION 2.0.0
    DESCRIPTION "Industrial communication intelligent diagnostic platform (Modbus)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# BUILD_TESTING is defined by include(CTest), defaults to ON.
include(CTest)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 SerialPort Network Test)
qt_standard_project_setup()

# ------------------------------------------------------------------- core ----
# Pure C++ protocol/analysis core. Deliberately links no Qt at all: the same
# logic must be shared by Simulator/Replay/Serial and stay unit-testable and
# reusable even if Qt is dropped (see docs/02_ARCHITECTURE.md D3).
add_library(modbuslens_core STATIC
    src/core/protocol/ModbusCrc.cpp
    src/core/protocol/ModbusRtuCodec.cpp
    src/core/protocol/Function03.cpp
    src/core/protocol/Function06.cpp
    src/core/protocol/Function16.cpp
    src/core/simulator/SimulatedSlave.cpp
    src/core/simulator/SimulationFault.cpp
    src/core/analysis/TransactionAnalysis.cpp
    src/core/analysis/RegisterDecode.cpp
    src/core/profile/DeviceProfile.cpp
    src/core/manual/ManualDocument.cpp
    src/core/candidate/CandidateExtraction.cpp
    src/core/candidate/ProviderExtractionContract.cpp
    src/core/analysis/PassiveTransactionAnalysis.cpp
    src/core/analysis/TransactionProvenance.cpp
    src/core/analysis/TransactionStatistics.cpp
    src/core/active/ActiveRequestIntent.cpp
    src/core/active/ActiveTransactionEvidence.cpp
    src/core/active/WriteDraftParsing.cpp
    src/core/active/ReadRequestParsing.cpp
    src/core/active/WritePrepareValidation.cpp
    src/core/active/PreparedWriteSnapshot.cpp
    src/core/replay/ReplayLog.cpp
    src/core/replay/ReplayAnalysis.cpp
    src/core/serial/SerialTransactionSession.cpp
    src/core/diagnosis/DiagnosisContext.cpp
    src/core/diagnosis/RuleBasedDiagnosis.cpp
)
target_include_directories(modbuslens_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(modbuslens_core PRIVATE -Wall -Wextra)
endif()

# ------------------------------------------------------------- application --
# Qt Quick app (ADR001): QML is the final UI; the old QWidget/QMainWindow
# bootstrap and the Widgets dependency were removed in T008 Part A.
#
# The QML module is attached DIRECTLY to the executable target (standard Qt
# app-template structure): its registration objects then belong to the exe
# itself, so static linking cannot accidentally drop the module
# registration. The bridge tests compile the adapter sources directly (see
# tests section) — no QML duplication anywhere.
# M12-C C1b: offline-materialized PDF / DOCX extraction dependencies.
#
# The dependency bytes are never vendored and never downloaded by CMake. The tracked
# truth is third_party/m12c-c1b/dependencies.lock.json; the git-ignored materialized
# root is produced by scripts/materialize_c1b_deps.py (no network). Every hash is
# re-checked by the materializer before use, and the artifact hashes recorded in the
# materialized manifest are asserted again here so a stray swap cannot go unnoticed.
# ---------------------------------------------------------------------------
    # The materialized root is repo-relative (the lock manifest says
    # outputRoot = build/deps/m12c-c1b), deliberately shared by every build tree so a
    # Debug and a Release configuration consume the very same dependency bytes.
    set(MODBUSLENS_C1B_DEPS_ROOT "${CMAKE_SOURCE_DIR}/build/deps/m12c-c1b")
    set(MODBUSLENS_C1B_MANIFEST
        "${MODBUSLENS_C1B_DEPS_ROOT}/manifest/materialized.json")
    if(NOT EXISTS "${MODBUSLENS_C1B_MANIFEST}")
        message(FATAL_ERROR
            "M12-C C1b dependency root is missing or was never materialized.\n"
            "  expected manifest: ${MODBUSLENS_C1B_MANIFEST}\n"
            "  prepare it with (this never uses the network):\n"
            "    python scripts/materialize_c1b_deps.py\n"
            "  see third_party/m12c-c1b/dependencies.lock.json for the exact pins, and\n"
            "  set MODBUSLENS_BUILD_C1B_EXTRACTION_TESTS=OFF to build without them.")
    endif()

    file(READ "${MODBUSLENS_C1B_MANIFEST}" MODBUSLENS_C1B_MANIFEST_JSON)
    string(JSON MODBUSLENS_C1B_PDFIUM_INCLUDE GET "${MODBUSLENS_C1B_MANIFEST_JSON}"
           pdfium includeDir)
    string(JSON MODBUSLENS_C1B_PDFIUM_DLL GET "${MODBUSLENS_C1B_MANIFEST_JSON}"
           pdfium runtimeDll)
    string(JSON MODBUSLENS_C1B_PDFIUM_DLL_SHA GET "${MODBUSLENS_C1B_MANIFEST_JSON}"
           pdfium runtimeDllSha256)
    string(JSON MODBUSLENS_C1B_PDFIUM_IMPLIB GET "${MODBUSLENS_C1B_MANIFEST_JSON}"
           pdfium importLibrary)
    string(JSON MODBUSLENS_C1B_PDFIUM_IMPLIB_SHA GET "${MODBUSLENS_C1B_MANIFEST_JSON}"
           pdfium importLibrarySha256)
    string(JSON MODBUSLENS_C1B_LIBZIP_INCLUDE GET "${MODBUSLENS_C1B_MANIFEST_JSON}"
           libzip includeDir)
    string(JSON MODBUSLENS_C1B_LIBZIP_LIB GET "${MODBUSLENS_C1B_MANIFEST_JSON}"
           libzip staticLibrary)
    string(JSON MODBUSLENS_C1B_ZLIB_INCLUDE GET "${MODBUSLENS_C1B_MANIFEST_JSON}"
           zlib header)
    string(JSON MODBUSLENS_C1B_ZLIB_LIB GET "${MODBUSLENS_C1B_MANIFEST_JSON}"
           zlib library)
    string(JSON MODBUSLENS_C1B_PDFIUM_VERSION GET "${MODBUSLENS_C1B_MANIFEST_JSON}"
           pdfium version)

    foreach(_pair
            "MODBUSLENS_C1B_PDFIUM_INCLUDE;${MODBUSLENS_C1B_PDFIUM_INCLUDE}"
            "MODBUSLENS_C1B_PDFIUM_DLL;${MODBUSLENS_C1B_PDFIUM_DLL}"
            "MODBUSLENS_C1B_PDFIUM_IMPLIB;${MODBUSLENS_C1B_PDFIUM_IMPLIB}"
            "MODBUSLENS_C1B_LIBZIP_INCLUDE;${MODBUSLENS_C1B_LIBZIP_INCLUDE}"
            "MODBUSLENS_C1B_LIBZIP_LIB;${MODBUSLENS_C1B_LIBZIP_LIB}")
        list(GET _pair 0 _var)
        list(GET _pair 1 _value)
        # Every path in the materialized manifest is repo-relative; a re-run from another
        # drive keeps them absolute, so both forms are accepted here.
        if(IS_ABSOLUTE "${_value}")
            set(${_var} "${_value}")
        else()
            set(${_var} "${CMAKE_SOURCE_DIR}/${_value}")
        endif()
    endforeach()
    get_filename_component(MODBUSLENS_C1B_ZLIB_INCLUDE
                           "${MODBUSLENS_C1B_ZLIB_INCLUDE}" DIRECTORY)

    # Hash assertions: the manifest is build output but the hashes inside it must match
    # the tracked lock manifest, so a swapped artifact cannot silently enter a build.
    file(SHA256 "${MODBUSLENS_C1B_PDFIUM_DLL}" _pdfium_dll_sha)
    file(SHA256 "${MODBUSLENS_C1B_PDFIUM_IMPLIB}" _pdfium_implib_sha)
    if(NOT _pdfium_dll_sha STREQUAL MODBUSLENS_C1B_PDFIUM_DLL_SHA)
        message(FATAL_ERROR "M12-C C1b: pdfium.dll hash mismatch (expected "
                            "${MODBUSLENS_C1B_PDFIUM_DLL_SHA}, got ${_pdfium_dll_sha})")
    endif()
    if(NOT _pdfium_implib_sha STREQUAL MODBUSLENS_C1B_PDFIUM_IMPLIB_SHA)
        message(FATAL_ERROR "M12-C C1b: pdfium.dll.lib hash mismatch (expected "
                            "${MODBUSLENS_C1B_PDFIUM_IMPLIB_SHA}, got ${_pdfium_implib_sha})")
    endif()
    if(NOT EXISTS "${MODBUSLENS_C1B_LIBZIP_LIB}")
        message(FATAL_ERROR "M12-C C1b: libzip static library missing: "
                            "${MODBUSLENS_C1B_LIBZIP_LIB}\n"
                            "  re-run: python scripts/materialize_c1b_deps.py")
    endif()
    message(STATUS
        "M12-C C1b dependencies: PDFium ${MODBUSLENS_C1B_PDFIUM_VERSION} (pinned, "
        "sha256 ${_pdfium_dll_sha}) + libzip (static) - offline materialized root in use")

    add_library(modbuslens_pdfium SHARED IMPORTED GLOBAL)
    set_target_properties(modbuslens_pdfium PROPERTIES
        IMPORTED_LOCATION "${MODBUSLENS_C1B_PDFIUM_DLL}"
        IMPORTED_IMPLIB "${MODBUSLENS_C1B_PDFIUM_IMPLIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${MODBUSLENS_C1B_PDFIUM_INCLUDE}")

    add_library(modbuslens_libzip STATIC IMPORTED GLOBAL)
    set_target_properties(modbuslens_libzip PROPERTIES
        IMPORTED_LOCATION "${MODBUSLENS_C1B_LIBZIP_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${MODBUSLENS_C1B_LIBZIP_INCLUDE}")

    add_library(modbuslens_zlib STATIC IMPORTED GLOBAL)
    set_target_properties(modbuslens_zlib PROPERTIES
        IMPORTED_LOCATION "${MODBUSLENS_C1B_ZLIB_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${MODBUSLENS_C1B_ZLIB_INCLUDE}")

        # M12-C C1b second slice: the application now links the same IMPORTED targets
        # (see the app section above); these tests reuse them unchanged.

qt_add_executable(modbuslens WIN32
    src/main.cpp
)

# M9-E E1: the CMake project VERSION is the ONLY public product-version
# authority (user release decision, see T020 "M9-E Version Decision"); the
# generated header feeds QCoreApplication::applicationVersion and the
# generated Windows VERSIONINFO carries the PE identity. Generated outputs
# live in the build tree and are never committed. The .rc is Windows-only and
# carries NO icon (E2 adds the PE icon).
set(MODBUSLENS_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${MODBUSLENS_GENERATED_DIR}/modbuslens_version.h"
)
target_include_directories(modbuslens PRIVATE "${MODBUSLENS_GENERATED_DIR}")
# M12-B: the qmltyperegistrar generates <ProfileController.h> for the
# QML_ELEMENT type in this subdirectory; without this include path the
# generated __has_include fails and the type is never registered.
target_include_directories(modbuslens PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/profile")
# M12-C C1a: same qmltyperegistrar requirement for the manual controllers.
target_include_directories(modbuslens PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/manual")
# M12-C C2 third slice: same requirement for the candidate orchestration owner.
target_include_directories(modbuslens PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/candidate")
if(WIN32)
    set(MODBUSLENS_ICO_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/windows/ModbusLens.ico")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows/ModbusLens.rc.in"
        "${MODBUSLENS_GENERATED_DIR}/ModbusLens.rc"
    )
    target_sources(modbuslens PRIVATE "${MODBUSLENS_GENERATED_DIR}/ModbusLens.rc")
endif()
# Note on DesignSystem.qml (M9-A): it is deliberately a PLAIN QML file (no
# pragma Singleton). The module-singleton route (pragma Singleton +
# QT_QML_SINGLETON_TYPE) compiled and generated the qmldir correctly but
# still failed at runtime with "DS is not defined" (ISSUE-010); the tokens
# are therefore exposed via an engine context property created in main.cpp.
qt_add_qml_module(modbuslens
    URI ModbusLens
    VERSION 1.0
    # M9-E E2: the committed Windows ICO doubles as the Qt runtime window
    # icon resource (embedded in the exe; no loose deploy asset). The PE
    # icon comes from the same file via ModbusLens.rc.in.
    RESOURCES
        assets/brand/windows/ModbusLens.ico
    QML_FILES
        src/ui/qml/Main.qml
        src/ui/qml/DS/DesignSystem.qml
        src/ui/qml/components/AppButton.qml
        src/ui/qml/components/DecimalField.qml
        src/ui/qml/components/NavigationRail.qml
        src/ui/qml/components/PanelCard.qml
        src/ui/qml/components/SectionHeader.qml
        src/ui/qml/components/StatCard.qml
        src/ui/qml/components/StatisticsMetrics.qml
        src/ui/qml/components/StatisticsOutcomes.qml
        src/ui/qml/components/OutcomeDistribution.qml
        src/ui/qml/components/StatisticsOverview.qml
        src/ui/qml/components/WriteFoundationSection.qml
        src/ui/qml/pages/DashboardPage.qml
        src/ui/qml/pages/CommunicationPage.qml
        src/ui/qml/pages/ReplayPage.qml
        src/ui/qml/pages/DiagnosisPage.qml
        src/ui/qml/pages/TransactionsPage.qml
        src/ui/qml/pages/DeviceProfilePage.qml
    SOURCES
        src/ui/AnalysisController.h
        src/ui/AnalysisController.cpp
        src/ui/manual/ManualStore.h
        src/ui/manual/ManualStore.cpp
        src/ui/manual/ManualImportController.h
        src/ui/manual/ManualImportController.cpp
        src/ui/manual/ManualTextExtraction.h
        src/ui/manual/ManualTextExtraction.cpp
        src/ui/manual/ManualPdfTextExtractor.h
        src/ui/manual/ManualPdfTextExtractor.cpp
        src/ui/manual/ManualDocxTextExtractor.h
        src/ui/manual/ManualDocxTextExtractor.cpp
        src/ui/candidate/CandidateExtractionRunner.h
        src/ui/candidate/ModelScopeCandidateRunner.h
        src/ui/candidate/ModelScopeCandidateRunner.cpp
        src/ui/candidate/CandidateExtractionController.h
        src/ui/candidate/CandidateExtractionController.cpp
        src/ui/candidate/ModelScopeHttpClient.h
        src/ui/candidate/ModelScopeHttpClient.cpp
        src/ui/candidate/ModelScopeExtractionTransport.h
        src/ui/candidate/ModelScopeExtractionTransport.cpp
        src/ui/ai/ModelScopeCandidateAdapter.h
        src/ui/ai/ModelScopeCandidateAdapter.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp
        src/ui/profile/ProfileController.h
        src/ui/profile/ProfileController.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp
        src/ui/profile/ProfileController.h
        src/ui/profile/ProfileController.cpp
        src/ui/profile/ActiveProfileController.h
        src/ui/profile/ActiveProfileController.cpp
        src/ui/TransactionListModel.h
        src/ui/TransactionListModel.cpp
        src/ui/serial/SerialTransport.h
        src/ui/serial/SerialTransport.cpp
        src/ui/serial/SerialPortAdapter.h
        src/ui/serial/SerialPortAdapter.cpp
        src/ui/ai/ModelScopeDiagnosisClient.h
        src/ui/ai/ModelScopeDiagnosisClient.cpp
        src/ui/ai/DiagnosisPromptBuilder.h
        src/ui/ai/DiagnosisPromptBuilder.cpp
        src/ui/agent/AgentToolContext.h
        src/ui/agent/AgentToolContext.cpp
        src/ui/agent/AgentTools.h
        src/ui/agent/AgentTools.cpp
        src/ui/agent/AgentPromptBuilder.h
        src/ui/agent/AgentPromptBuilder.cpp
        src/ui/agent/ModelScopeAgentClient.h
        src/ui/agent/ModelScopeAgentClient.cpp
        src/ui/agent/AgentRuntime.h
        src/ui/agent/AgentRuntime.cpp
)
target_include_directories(modbuslens PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
# Link graph (T010 Part B + T011 Part B): the APP links Qt6::SerialPort
# (serial adapter) and Qt6::Network (ModelScope client) — modbuslens_core
# stays Zero Qt / Zero Network.
# M12-C C1b second slice: the application itself now performs PDF/DOCX extraction,
# so the app consumes the SAME offline-materialized IMPORTED targets as the tests.
target_link_libraries(modbuslens PRIVATE
    modbuslens_core Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::SerialPort Qt6::Network
    modbuslens_pdfium modbuslens_libzip modbuslens_zlib)
# Dev-run convenience: the pinned pdfium.dll must sit next to the exe (never resolved
# from PATH or a toolchain directory). Packaging rounds must ship it as well.
add_custom_command(TARGET modbuslens POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MODBUSLENS_C1B_PDFIUM_DLL}"
            "$<TARGET_FILE_DIR:modbuslens>/pdfium.dll"
    COMMENT "Staging the pinned pdfium.dll next to the application")

    # ------------------------------------------------------------------
    # M12 Windows deployment architecture (Session H): RAW BUILD OUTPUT
    # != DEPLOYABLE CANDIDATE. The only deployable artifact root for this
    # configuration is ${CMAKE_BINARY_DIR}/candidate/ModbusLens, generated
    # FROM SCRATCH by cmake/modbuslens_generate_candidate.cmake with
    # identity-bound sources only: the current app target, the frozen PDFium
    # materialization, the active MinGW toolchain and the active Qt kit.
    # The raw target directory is a compile output, never a candidate.
    # ------------------------------------------------------------------
    get_filename_component(MODBUSLENS_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(MODBUSLENS_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    add_custom_target(modbuslens_candidate
        COMMAND ${CMAKE_COMMAND}
            -DCANDIDATE_DIR=${CMAKE_BINARY_DIR}/candidate/ModbusLens
            -DAPP_EXE=$<TARGET_FILE:modbuslens>
            -DPDFIUM_DLL=${MODBUSLENS_C1B_PDFIUM_DLL}
            -DCOMPILER_BIN=${MODBUSLENS_COMPILER_BIN}
            -DQT_PREFIX=${MODBUSLENS_QT_PREFIX}
            -DQML_MODULE_DIR=${CMAKE_BINARY_DIR}/ModbusLens
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modbuslens_generate_candidate.cmake"
        DEPENDS modbuslens
        COMMENT "Generating the self-contained ModbusLens candidate")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(modbuslens PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------- tests --
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    # The old QMainWindow smoke test (T001) was removed with the QWidget
    # bootstrap in T008 Part A; app-level QML smoke is `qml_smoke` below.

    add_executable(modbuslens_crc_tests tests/test_modbus_crc.cpp)
    target_link_libraries(modbuslens_crc_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_crc_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME crc COMMAND modbuslens_crc_tests)

    add_executable(modbuslens_frame_tests tests/test_modbus_rtu_frame.cpp)
    target_link_libraries(modbuslens_frame_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_frame_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME frame COMMAND modbuslens_frame_tests)

    add_executable(modbuslens_codec_tests tests/test_modbus_rtu_codec.cpp)
    target_link_libraries(modbuslens_codec_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_codec_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME codec COMMAND modbuslens_codec_tests)

    add_executable(modbuslens_function03_tests tests/test_function03.cpp)
    target_link_libraries(modbuslens_function03_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_function03_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME f03 COMMAND modbuslens_function03_tests)

    add_executable(modbuslens_simulator_tests tests/test_simulated_slave.cpp)
    target_link_libraries(modbuslens_simulator_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_simulator_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME simulator COMMAND modbuslens_simulator_tests)

    add_executable(modbuslens_simulator_integration_tests tests/test_simulator_integration.cpp)
    target_link_libraries(modbuslens_simulator_integration_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_simulator_integration_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME simulator_integration COMMAND modbuslens_simulator_integration_tests)

    add_executable(modbuslens_fault_tests tests/test_simulation_fault.cpp)
    target_link_libraries(modbuslens_fault_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_fault_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME fault COMMAND modbuslens_fault_tests)

    add_executable(modbuslens_fault_integration_tests tests/test_fault_integration.cpp)
    target_link_libraries(modbuslens_fault_integration_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_fault_integration_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME fault_integration COMMAND modbuslens_fault_integration_tests)

    add_executable(modbuslens_transaction_tests tests/test_transaction_analysis.cpp)
    target_link_libraries(modbuslens_transaction_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_transaction_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME transaction COMMAND modbuslens_transaction_tests)

    add_executable(modbuslens_transaction_integration_tests tests/test_transaction_integration.cpp)
    target_link_libraries(modbuslens_transaction_integration_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_transaction_integration_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME transaction_integration COMMAND modbuslens_transaction_integration_tests)

    add_executable(modbuslens_statistics_tests tests/test_transaction_statistics.cpp)
    target_link_libraries(modbuslens_statistics_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_statistics_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME statistics COMMAND modbuslens_statistics_tests)

    add_executable(modbuslens_statistics_integration_tests tests/test_statistics_integration.cpp)
    target_link_libraries(modbuslens_statistics_integration_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_statistics_integration_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME statistics_integration COMMAND modbuslens_statistics_integration_tests)

    add_executable(modbuslens_ui_bridge_tests tests/test_ui_bridge.cpp
        tests/fake_chat_completions_server.cpp
        # M10-B: the serial bridge tests drive the REAL Active Serial path
        # through the deterministic recording transport (no COM port).
        tests/fake_serial_transport.h
        tests/fake_serial_transport.cpp
        src/ui/AnalysisController.h
        src/ui/AnalysisController.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp
        src/ui/profile/ProfileController.h
        src/ui/profile/ProfileController.cpp
        src/ui/profile/ActiveProfileController.h
        src/ui/profile/ActiveProfileController.cpp
        src/ui/TransactionListModel.h
        src/ui/TransactionListModel.cpp
        src/ui/serial/SerialTransport.h
        src/ui/serial/SerialTransport.cpp
        src/ui/serial/SerialPortAdapter.h
        src/ui/serial/SerialPortAdapter.cpp
        src/ui/ai/ModelScopeDiagnosisClient.h
        src/ui/ai/ModelScopeDiagnosisClient.cpp
        src/ui/ai/DiagnosisPromptBuilder.h
        src/ui/ai/DiagnosisPromptBuilder.cpp
        src/ui/agent/AgentToolContext.h
        src/ui/agent/AgentToolContext.cpp
        src/ui/agent/AgentTools.h
        src/ui/agent/AgentTools.cpp
        src/ui/agent/AgentPromptBuilder.h
        src/ui/agent/AgentPromptBuilder.cpp
        src/ui/agent/ModelScopeAgentClient.h
        src/ui/agent/ModelScopeAgentClient.cpp
        src/ui/agent/AgentRuntime.h
        src/ui/agent/AgentRuntime.cpp)
    target_include_directories(modbuslens_ui_bridge_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_compile_definitions(modbuslens_ui_bridge_tests PRIVATE
        MODBUSLENS_DEMO_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/demo_v1.mlog")
    target_link_libraries(modbuslens_ui_bridge_tests PRIVATE
        modbuslens_core Qt6::Qml Qt6::SerialPort Qt6::Network Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_ui_bridge_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME ui_bridge COMMAND modbuslens_ui_bridge_tests)

    # Replay (T009): the CANONICAL golden sample lives in samples/ (single
    # source shared by tests, deployment and the manual smoke). It is copied
    # into the build tree and only the test targets learn that path via a
    # compile definition — no absolute user-dependent paths in product code.
    configure_file(samples/demo_v1.mlog
        "${CMAKE_CURRENT_BINARY_DIR}/test_data/demo_v1.mlog" COPYONLY)
    # T015 unsupported-function sample: drives the replayNotice disclosure
    # (Gate F) in the M9-B4.3 navigation scenarios.
    configure_file(samples/t015_unsupported_fc08.mlog
        "${CMAKE_CURRENT_BINARY_DIR}/test_data/t015_unsupported_fc08.mlog" COPYONLY)
    # T015 broadcast sample (one FC06 broadcast with NO_RESPONSE): drives the
    # M9-C C3 distribution-denominator probe -- a batch whose completed
    # outcomes are ALL ExpectedNoResponse, so the distribution bar is 100%
    # ExpectedNoResponse while successRate stays undefined ("—").
    configure_file(samples/t015_broadcast.mlog
        "${CMAKE_CURRENT_BINARY_DIR}/test_data/t015_broadcast.mlog" COPYONLY)
    # T014 protocol-error sample (FC06 response echoing a different register
    # value -> WriteSingleRegisterEchoMismatch): drives the M9-C C4 attention
    # probe -- protocolErrorCount > 0 must contribute to the attention sum.
    configure_file(samples/t014_protocol_error.mlog
        "${CMAKE_CURRENT_BINARY_DIR}/test_data/t014_protocol_error.mlog" COPYONLY)

    add_executable(modbuslens_replay_log_tests tests/test_replay_log.cpp)
    target_link_libraries(modbuslens_replay_log_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_replay_log_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME replay_log COMMAND modbuslens_replay_log_tests)

    add_executable(modbuslens_replay_analysis_tests tests/test_replay_analysis.cpp)
    target_compile_definitions(modbuslens_replay_analysis_tests PRIVATE
        MODBUSLENS_DEMO_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/demo_v1.mlog")
    target_link_libraries(modbuslens_replay_analysis_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_replay_analysis_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME replay_analysis COMMAND modbuslens_replay_analysis_tests)

    # T015 Passive Replay Expansion: the generic passive analyzer lives in
    # Core; its tests exercise the real replay path end to end.
    add_executable(modbuslens_passive_tests tests/test_passive_analysis.cpp)
    target_link_libraries(modbuslens_passive_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_passive_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME passive COMMAND modbuslens_passive_tests)

    # Serial (T010 Part A): Pure transaction session needs no Qt beyond the
    # test framework; the adapter test is the ONLY target that links
    # Qt6::SerialPort (the app binary does NOT link it until Part B wires the
    # adapter into the UI — documented in the T010 task archive).
    add_executable(modbuslens_serial_tests tests/test_serial_session.cpp)
    target_link_libraries(modbuslens_serial_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_serial_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME serial COMMAND modbuslens_serial_tests)

    # M10-A: unified active-request intent + generic session contract
    # (Pure Core: intent validation/encoding, descriptor self-consistency,
    # pending snapshot, frozen echo/orthogonality/broadcast semantics).
    add_executable(modbuslens_active_request_tests tests/test_active_request.cpp)
    target_link_libraries(modbuslens_active_request_tests PRIVATE
        modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_active_request_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME active_request COMMAND modbuslens_active_request_tests)

    # M10-C1: pure write-prepare foundation (parser / validation / prepared
    # snapshot state machine). Zero Qt beyond the test framework; no encoder,
    # no transport, no UI.
    add_executable(modbuslens_write_prepare_tests tests/test_write_prepare.cpp)
    target_link_libraries(modbuslens_write_prepare_tests PRIVATE
        modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_write_prepare_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME write_prepare COMMAND modbuslens_write_prepare_tests)

    # M10-D1: the FC06 encoder + its INDEPENDENT golden-vector oracle (the
    # test file carries its own bitwise CRC, so the encoder can never prove
    # itself from a shared helper).
    add_executable(modbuslens_write_encoder_tests tests/test_write_encoder.cpp)
    target_link_libraries(modbuslens_write_encoder_tests PRIVATE
        modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_write_encoder_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME write_encoder COMMAND modbuslens_write_encoder_tests)

    # M10-D2: the FC06 ACTIVE response suite (framing, shared analyzer,
    # fragmentation, error classes, support matrix). Kept as its own target so
    # the generic session suite stays readable.
    add_executable(modbuslens_fc06_active_tests tests/test_fc06_active.cpp)
    target_link_libraries(modbuslens_fc06_active_tests PRIVATE
        modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_fc06_active_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME fc06_active COMMAND modbuslens_fc06_active_tests)

    # M11 first slice: the single-register decode view (pure core decode layer
    # over the canonical raw uint16 words; independent derivations in the test).
    add_executable(modbuslens_register_decode_tests tests/test_register_decode.cpp)
    target_link_libraries(modbuslens_register_decode_tests PRIVATE
        modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_register_decode_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME register_decode COMMAND modbuslens_register_decode_tests)

    # M12-A first slice: device-profile model + validation (core, Zero Qt) and
    # the JSON persistence layer (Qt side) share one deterministic target.
    add_executable(modbuslens_device_profile_tests tests/test_device_profile.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp)
    target_link_libraries(modbuslens_device_profile_tests PRIVATE
        modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_device_profile_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME device_profile COMMAND modbuslens_device_profile_tests)

    # M12-B first slice: the UI-layer profile controller (catalog + identity
    # draft + dirty state + file lifecycle), driven against an injected
    # temporary managed root (never the real user data directory).
    add_executable(modbuslens_profile_controller_tests
        tests/test_profile_controller.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp
        src/ui/profile/ProfileController.h
        src/ui/profile/ProfileController.cpp)
    target_link_libraries(modbuslens_profile_controller_tests PRIVATE
        modbuslens_core Qt6::Qml Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_profile_controller_tests PRIVATE
            -Wall -Wextra)
    endif()

    add_test(NAME profile_controller
             COMMAND modbuslens_profile_controller_tests)

    # M12-B third slice: session Active Profile selection (identity =
    # full profileId, startup = none, no persistence, no auto-binding),
    # driven against an injected temporary managed root.
    add_executable(modbuslens_active_profile_controller_tests
        tests/test_active_profile_controller.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp
        src/ui/profile/ProfileController.h
        src/ui/profile/ProfileController.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp
        src/ui/profile/ProfileController.h
        src/ui/profile/ProfileController.cpp
        src/ui/profile/ActiveProfileController.h
        src/ui/profile/ActiveProfileController.cpp)
    target_link_libraries(modbuslens_active_profile_controller_tests PRIVATE
        modbuslens_core Qt6::Qml Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_active_profile_controller_tests
            PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME active_profile_controller
             COMMAND modbuslens_active_profile_controller_tests)

    # M12-C C1a: deterministic manual import (TXT / Markdown) + evidence
    # foundation, driven against an injected temporary managed root. The
    # profile sources are linked here on purpose: the same target asserts the
    # isolation invariants (draft / dirty / Active Profile / profile JSON).
    add_executable(modbuslens_manual_import_tests
        tests/test_manual_import.cpp
        src/ui/manual/ManualStore.h
        src/ui/manual/ManualStore.cpp
        src/ui/manual/ManualImportController.h
        src/ui/manual/ManualImportController.cpp
        src/ui/manual/ManualTextExtraction.h
        src/ui/manual/ManualTextExtraction.cpp
        src/ui/manual/ManualPdfTextExtractor.h
        src/ui/manual/ManualPdfTextExtractor.cpp
        src/ui/manual/ManualDocxTextExtractor.h
        src/ui/manual/ManualDocxTextExtractor.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp
        src/ui/profile/ProfileController.h
        src/ui/profile/ProfileController.cpp
        src/ui/profile/ActiveProfileController.h
        src/ui/profile/ActiveProfileController.cpp)
    target_include_directories(modbuslens_manual_import_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(modbuslens_manual_import_tests PRIVATE
        modbuslens_core Qt6::Qml Qt6::Test modbuslens_pdfium modbuslens_libzip
        modbuslens_zlib)
    # The C1a regression suite now exercises the PDF/DOCX-aware store, so it
    # needs the pinned PDFium runtime beside it as well.
    add_custom_command(TARGET modbuslens_manual_import_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MODBUSLENS_C1B_PDFIUM_DLL}"
                "$<TARGET_FILE_DIR:modbuslens_manual_import_tests>/pdfium.dll"
        COMMENT "Staging the pinned pdfium.dll next to the C1a import tests")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_manual_import_tests PRIVATE
            -Wall -Wextra)
    endif()

    add_test(NAME manual_import
             COMMAND modbuslens_manual_import_tests)

    # ---------------------------------------------------------------------------
    # M12-C C2 first deterministic slice (T027 §66): provider-neutral candidate
    # extraction with locally verified evidence. The domain lives in the
    # Zero-Qt core (src/core/candidate) and is linked straight from
    # modbuslens_core; this target adds the Qt side only for the persistence
    # isolation assertions (ProfileStore root/dir queries) and QTest.
    add_executable(modbuslens_candidate_extraction_tests
        tests/test_candidate_extraction.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp)
    target_include_directories(modbuslens_candidate_extraction_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(modbuslens_candidate_extraction_tests PRIVATE
        modbuslens_core Qt6::Core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_candidate_extraction_tests PRIVATE
            -Wall -Wextra)
    endif()

    add_test(NAME candidate_extraction
             COMMAND modbuslens_candidate_extraction_tests)

    # ---------------------------------------------------------------------------
    # M12-C C2 SECOND SLICE (T027 §68): ModelScope provider adapter boundary +
    # strict provider response contract, exercised ONLY through a deterministic
    # fake transport. This target deliberately does NOT link Qt6::Network — the
    # adapter under test has no network capability at all, so no live request
    # is possible from the automated path.
    add_executable(modbuslens_candidate_adapter_tests
        tests/test_candidate_adapter.cpp
        src/ui/ai/ModelScopeCandidateAdapter.h
        src/ui/ai/ModelScopeCandidateAdapter.cpp
        src/ui/ai/ExtractionTransport.h
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp)
    target_include_directories(modbuslens_candidate_adapter_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(modbuslens_candidate_adapter_tests PRIVATE
        modbuslens_core Qt6::Core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_candidate_adapter_tests PRIVATE
            -Wall -Wextra)
    endif()

    add_test(NAME candidate_adapter
             COMMAND modbuslens_candidate_adapter_tests)

    # ---------------------------------------------------------------------------
    # M12-C C2 THIRD SLICE (T027 §70): production orchestration + the cloud
    # consent gate + the SESSION-ONLY PendingReview Candidate set. The
    # orchestration layer is driven by an injected deterministic fake runner;
    # the manual sources are linked because the orchestrator reads the selected
    # document through ManualImportController and the canonical text through
    # ManualStore (never the original file).
    add_executable(modbuslens_candidate_orchestration_tests
        tests/test_candidate_orchestration.cpp
        src/ui/ai/ModelScopeCandidateAdapter.h
        src/ui/ai/ModelScopeCandidateAdapter.cpp
        src/ui/candidate/CandidateExtractionRunner.h
        src/ui/candidate/ModelScopeCandidateRunner.h
        src/ui/candidate/ModelScopeCandidateRunner.cpp
        src/ui/candidate/CandidateExtractionController.h
        src/ui/candidate/CandidateExtractionController.cpp
        src/ui/candidate/ModelScopeHttpClient.h
        src/ui/candidate/ModelScopeHttpClient.cpp
        src/ui/candidate/ModelScopeExtractionTransport.h
        src/ui/candidate/ModelScopeExtractionTransport.cpp
        src/ui/manual/ManualStore.h
        src/ui/manual/ManualStore.cpp
        src/ui/manual/ManualImportController.h
        src/ui/manual/ManualImportController.cpp
        src/ui/manual/ManualTextExtraction.h
        src/ui/manual/ManualTextExtraction.cpp
        src/ui/manual/ManualPdfTextExtractor.h
        src/ui/manual/ManualPdfTextExtractor.cpp
        src/ui/manual/ManualDocxTextExtractor.h
        src/ui/manual/ManualDocxTextExtractor.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp)
    target_include_directories(modbuslens_candidate_orchestration_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    # Qt6::Network is required from SESSION P on: the orchestration owner now
    # constructs the PRODUCTION runner, which reaches the Qt Network transport.
    # The suite itself still injects a fake runner, so nothing here opens a
    # socket.
    target_link_libraries(modbuslens_candidate_orchestration_tests PRIVATE
        modbuslens_core Qt6::Core Qt6::Test Qt6::Network modbuslens_pdfium
        modbuslens_libzip modbuslens_zlib)
    add_custom_command(TARGET modbuslens_candidate_orchestration_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MODBUSLENS_C1B_PDFIUM_DLL}"
                "$<TARGET_FILE_DIR:modbuslens_candidate_orchestration_tests>/pdfium.dll"
        COMMENT "Staging the pinned pdfium.dll next to the orchestration tests")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_candidate_orchestration_tests PRIVATE
            -Wall -Wextra)
    endif()

    add_test(NAME candidate_orchestration
             COMMAND modbuslens_candidate_orchestration_tests)

    # ---------------------------------------------------------------------------
    # M12-C C2 FOURTH SLICE (T027 §76): the PRODUCTION ModelScope transport, the
    # process-environment credential/config seam and the full consent -> HTTP ->
    # envelope -> strict parser -> Evidence chain. The production transport code
    # path is exercised for real; only the thin HTTP seam is faked, so no socket
    # is ever opened and the suite is fully deterministic and offline.
    add_executable(modbuslens_candidate_transport_tests
        tests/test_candidate_transport.cpp
        src/ui/ai/ModelScopeCandidateAdapter.h
        src/ui/ai/ModelScopeCandidateAdapter.cpp
        src/ui/candidate/CandidateExtractionRunner.h
        src/ui/candidate/ModelScopeCandidateRunner.h
        src/ui/candidate/ModelScopeCandidateRunner.cpp
        src/ui/candidate/CandidateExtractionController.h
        src/ui/candidate/CandidateExtractionController.cpp
        src/ui/candidate/ModelScopeHttpClient.h
        src/ui/candidate/ModelScopeHttpClient.cpp
        src/ui/candidate/ModelScopeExtractionTransport.h
        src/ui/candidate/ModelScopeExtractionTransport.cpp
        src/ui/manual/ManualStore.h
        src/ui/manual/ManualStore.cpp
        src/ui/manual/ManualImportController.h
        src/ui/manual/ManualImportController.cpp
        src/ui/manual/ManualTextExtraction.h
        src/ui/manual/ManualTextExtraction.cpp
        src/ui/manual/ManualPdfTextExtractor.h
        src/ui/manual/ManualPdfTextExtractor.cpp
        src/ui/manual/ManualDocxTextExtractor.h
        src/ui/manual/ManualDocxTextExtractor.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp)
    target_include_directories(modbuslens_candidate_transport_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(modbuslens_candidate_transport_tests PRIVATE
        modbuslens_core Qt6::Core Qt6::Test Qt6::Network modbuslens_pdfium
        modbuslens_libzip modbuslens_zlib)
    add_custom_command(TARGET modbuslens_candidate_transport_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MODBUSLENS_C1B_PDFIUM_DLL}"
                "$<TARGET_FILE_DIR:modbuslens_candidate_transport_tests>/pdfium.dll"
        COMMENT "Staging the pinned pdfium.dll next to the C2 transport tests")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_candidate_transport_tests PRIVATE
            -Wall -Wextra)
    endif()

    add_test(NAME candidate_transport
             COMMAND modbuslens_candidate_transport_tests)

    # ---------------------------------------------------------------------------
    # M12-C C2 SESSION P-R1 (T027 §77): the configuration contract is defined by
    # the SOURCE, not by a stale cache. commit fb2170e declared this option with
    # an ON default and commit c36eb18 removed the declaration while keeping the
    # guard below, so a genuinely fresh configure silently skipped the C1b
    # extraction/deployment gates (inventory dropped from 63 to 59) and only the
    # stale build/release cache kept them alive. Restoring the declaration makes
    # a fresh release-local configure register the canonical inventory again,
    # with no manual -D and no reliance on developer memory.
    option(MODBUSLENS_BUILD_C1B_EXTRACTION_TESTS
        "Build the M12-C C1b PDF/DOCX extraction foundation tests (requires the materialized dependency root)"
        ON)

    if(MODBUSLENS_BUILD_C1B_EXTRACTION_TESTS)
        add_executable(modbuslens_manual_extraction_tests
            tests/test_manual_extraction.cpp
            src/ui/manual/ManualTextExtraction.h
            src/ui/manual/ManualTextExtraction.cpp
            src/ui/manual/ManualPdfTextExtractor.h
            src/ui/manual/ManualPdfTextExtractor.cpp
            src/ui/manual/ManualDocxTextExtractor.h
            src/ui/manual/ManualDocxTextExtractor.cpp)
        target_link_libraries(modbuslens_manual_extraction_tests PRIVATE
            modbuslens_core Qt6::Core Qt6::Test modbuslens_pdfium modbuslens_libzip
            modbuslens_zlib)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(modbuslens_manual_extraction_tests PRIVATE -Wall -Wextra)
        endif()
        # The extractor tests must load the FROZEN pdfium.dll from their own directory: no
        # PATH entry, no Qt or toolchain directory and no other pdfium.dll copy may satisfy
        # the import.
        add_custom_command(TARGET modbuslens_manual_extraction_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MODBUSLENS_C1B_PDFIUM_DLL}"
                    "$<TARGET_FILE_DIR:modbuslens_manual_extraction_tests>/pdfium.dll"
            COMMENT "Staging the pinned pdfium.dll next to the extraction tests")

        add_test(NAME manual_extraction
                 COMMAND modbuslens_manual_extraction_tests)

        # M12-C C1b second slice: the IMPORT WORKFLOW (routing, atomicity,
        # no-text success, duplicate/cache identity) exercised through the real
        # store transaction.
        add_executable(modbuslens_manual_import_pdf_docx_tests
            tests/test_manual_import_pdf_docx.cpp
            src/ui/manual/ManualStore.h
            src/ui/manual/ManualStore.cpp
            src/core/manual/ManualDocument.h
            src/core/manual/ManualDocument.cpp
            src/ui/manual/ManualTextExtraction.h
            src/ui/manual/ManualTextExtraction.cpp
            src/ui/manual/ManualPdfTextExtractor.h
            src/ui/manual/ManualPdfTextExtractor.cpp
            src/ui/manual/ManualDocxTextExtractor.h
            src/ui/manual/ManualDocxTextExtractor.cpp)
        target_include_directories(modbuslens_manual_import_pdf_docx_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(modbuslens_manual_import_pdf_docx_tests PRIVATE
            Qt6::Core Qt6::Test modbuslens_pdfium modbuslens_libzip
            modbuslens_zlib)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(modbuslens_manual_import_pdf_docx_tests
                PRIVATE -Wall -Wextra)
        endif()
        add_custom_command(TARGET modbuslens_manual_import_pdf_docx_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MODBUSLENS_C1B_PDFIUM_DLL}"
                    "$<TARGET_FILE_DIR:modbuslens_manual_import_pdf_docx_tests>/pdfium.dll"
            COMMENT "Staging the pinned pdfium.dll next to the import workflow tests")
        add_test(NAME manual_import_pdf_docx
                 COMMAND modbuslens_manual_import_pdf_docx_tests)

        # M12-C C1b SESSION E: Explorer-style deployment-startup gate.
        #
        # The CTest worker PATH contains the developer toolchain directories, which
        # would silently mask a missing compiler/Qt runtime in the candidate. This
        # gate therefore launches the candidate through tests/deployment_startup_check.cmake,
        # which replaces PATH with candidate-dir + Windows system directories ONLY
        # (no developer Qt/MinGW bin fallback), verifies the candidate manifest and
        # the runtime hashes first, then asserts exit 0 of the finite
        # --qml-smoke-test launch with QT_DEBUG_PLUGINS proof that qwindows.dll
        # was loaded from the candidate tree.
        add_test(NAME deployment_startup_check
            COMMAND ${CMAKE_COMMAND}
                -DCANDIDATE_DIR=${CMAKE_BINARY_DIR}/candidate/ModbusLens
                -DEXE_NAME=modbuslens.exe
                -DCOMPILER_BIN=${MODBUSLENS_COMPILER_BIN}
                -DQT_PREFIX=${MODBUSLENS_QT_PREFIX}
                -DQML_MODULE_DIR=${CMAKE_BINARY_DIR}/ModbusLens
                -DAPP_EXE=$<TARGET_FILE:modbuslens>
                -DPDFIUM_DLL=${MODBUSLENS_C1B_PDFIUM_DLL}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/deployment_startup_check.cmake")

        # Dependency materializer contract tests (offline; hash tampering must be refused).
        #
        # Two things are passed EXPLICITLY and never probed from PATH:
        #   * the interpreter: -DMODBUSLENS_PYTHON_EXECUTABLE=<path-to-python>
        #     (probing PATH with find_program() executes whatever python the environment
        #      exposes, which in this environment terminated the CMake regeneration process)
        #   * the pinned zlib identity: read from the git-ignored materialized manifest and
        #     forwarded as command arguments, so the test re-verifies the very same files
        #     the product consumes (no ambient MinGW discovery, no machine path literal here)
        string(JSON MODBUSLENS_C1B_ZLIB_HEADER GET "${MODBUSLENS_C1B_MANIFEST_JSON}" zlib header)
        string(JSON MODBUSLENS_C1B_ZLIB_LIBRARY GET "${MODBUSLENS_C1B_MANIFEST_JSON}" zlib library)
        set(MODBUSLENS_PYTHON_EXECUTABLE "" CACHE FILEPATH
            "Explicit Python interpreter for the M12-C C1b dependency contract tests (never probed from PATH)")
        if(MODBUSLENS_PYTHON_EXECUTABLE AND EXISTS "${MODBUSLENS_PYTHON_EXECUTABLE}")
            add_test(NAME c1b_dependency_materializer
                COMMAND "${MODBUSLENS_PYTHON_EXECUTABLE}"
                        "${CMAKE_SOURCE_DIR}/scripts/test_materialize_c1b_deps.py"
                        "--lock" "${CMAKE_SOURCE_DIR}/third_party/m12c-c1b/dependencies.lock.json"
                        "--distfiles" "${CMAKE_SOURCE_DIR}/third_party/m12c-c1b/distfiles"
                        "--zlib-include" "${MODBUSLENS_C1B_ZLIB_HEADER}"
                        "--zlib-library" "${MODBUSLENS_C1B_ZLIB_LIBRARY}")
            set_tests_properties(c1b_dependency_materializer PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        else()
            message(STATUS
                "M12-C C1b: c1b_dependency_materializer not registered (pass "
                "-DMODBUSLENS_PYTHON_EXECUTABLE=<python> to enable; PATH is never probed)")
        endif()
    endif()

    # M12-B slice 4: the read-result semantic projection, driven through the
    # REAL production read path (recording transport + shipped encoder +
    # shipped analyzer) against an injected temporary managed root.
    add_executable(modbuslens_profile_semantic_tests
        tests/test_profile_semantic.cpp
        tests/fake_serial_transport.h
        tests/fake_serial_transport.cpp
        src/ui/AnalysisController.h
        src/ui/AnalysisController.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp
        src/ui/profile/ProfileController.h
        src/ui/profile/ProfileController.cpp
        src/ui/profile/ActiveProfileController.h
        src/ui/profile/ActiveProfileController.cpp
        src/ui/TransactionListModel.h
        src/ui/TransactionListModel.cpp
        src/ui/serial/SerialTransport.h
        src/ui/serial/SerialTransport.cpp
        src/ui/serial/SerialPortAdapter.h
        src/ui/serial/SerialPortAdapter.cpp
        src/ui/ai/ModelScopeDiagnosisClient.h
        src/ui/ai/ModelScopeDiagnosisClient.cpp
        src/ui/ai/DiagnosisPromptBuilder.h
        src/ui/ai/DiagnosisPromptBuilder.cpp
        src/ui/agent/AgentToolContext.h
        src/ui/agent/AgentToolContext.cpp
        src/ui/agent/AgentTools.h
        src/ui/agent/AgentTools.cpp
        src/ui/agent/AgentPromptBuilder.h
        src/ui/agent/AgentPromptBuilder.cpp
        src/ui/agent/ModelScopeAgentClient.h
        src/ui/agent/ModelScopeAgentClient.cpp
        src/ui/agent/AgentRuntime.h
        src/ui/agent/AgentRuntime.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp
        src/ui/profile/ProfileController.h
        src/ui/profile/ProfileController.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp
        src/ui/profile/ProfileController.h
        src/ui/profile/ProfileController.cpp
        src/ui/profile/ActiveProfileController.h
        src/ui/profile/ActiveProfileController.cpp)
    target_include_directories(modbuslens_profile_semantic_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(modbuslens_profile_semantic_tests PRIVATE
        modbuslens_core Qt6::Qml Qt6::SerialPort Qt6::Network Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_profile_semantic_tests PRIVATE
            -Wall -Wextra)
    endif()

    add_test(NAME profile_semantic
             COMMAND modbuslens_profile_semantic_tests)

    # M12-B slice 4 gate: the Read Result three-layer semantic overlay,
    # driven end to end through the real Communication page, the real Read
    # Result dialog and the real production projection path.
    add_test(NAME qml_profile_semantic_check
        COMMAND modbuslens --qml-profile-semantic-check)

    # The deterministic visual demo in its TEST-ONLY mode (--demo-exit-after-
    # ready): asserts Scenario A (466 -> 46.6 Hz) and Scenario B (Float32
    # 2-word start/continuation) and exits by itself. The bare
    # --qml-profile-semantic-demo (no such flag) is the HUMAN mode and
    # deliberately never exits on its own.
    add_test(NAME qml_profile_semantic_demo
        COMMAND modbuslens --qml-profile-semantic-demo --demo-exit-after-ready)

    # M10-E2: the FC16/0x10 ACTIVE response suite (shared analyzer, framing,
    # error classes, single in-flight, support matrix). Kept as its own target
    # so the FC06 suite stays readable.
    add_executable(modbuslens_fc16_active_tests tests/test_fc16_active.cpp)
    target_link_libraries(modbuslens_fc16_active_tests PRIVATE
        modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_fc16_active_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME fc16_active COMMAND modbuslens_fc16_active_tests)

    # M10-A: Active Master contract foundation over the REAL controller with a
    # deterministic RECORDING transport (TA1-TA9): send counts, exact ADU
    # oracle, controlled completion timing, transmission disposition, wire
    # evidence retention, session append foundation, Clear contract.
    add_executable(modbuslens_active_master_tests
        tests/test_active_master.cpp
        tests/fake_serial_transport.h
        tests/fake_serial_transport.cpp
        src/ui/AnalysisController.h
        src/ui/AnalysisController.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp
        src/ui/profile/ProfileController.h
        src/ui/profile/ProfileController.cpp
        src/ui/profile/ActiveProfileController.h
        src/ui/profile/ActiveProfileController.cpp
        src/ui/TransactionListModel.h
        src/ui/TransactionListModel.cpp
        src/ui/serial/SerialTransport.h
        src/ui/serial/SerialTransport.cpp
        src/ui/serial/SerialPortAdapter.h
        src/ui/serial/SerialPortAdapter.cpp
        src/ui/ai/ModelScopeDiagnosisClient.h
        src/ui/ai/ModelScopeDiagnosisClient.cpp
        src/ui/ai/DiagnosisPromptBuilder.h
        src/ui/ai/DiagnosisPromptBuilder.cpp
        src/ui/agent/AgentToolContext.h
        src/ui/agent/AgentToolContext.cpp
        src/ui/agent/AgentTools.h
        src/ui/agent/AgentTools.cpp
        src/ui/agent/AgentPromptBuilder.h
        src/ui/agent/AgentPromptBuilder.cpp
        src/ui/agent/ModelScopeAgentClient.h
        src/ui/agent/ModelScopeAgentClient.cpp
        src/ui/agent/AgentRuntime.h
        src/ui/agent/AgentRuntime.cpp)
    target_include_directories(modbuslens_active_master_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_compile_definitions(modbuslens_active_master_tests PRIVATE
        MODBUSLENS_DEMO_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/demo_v1.mlog")
    target_link_libraries(modbuslens_active_master_tests PRIVATE
        modbuslens_core Qt6::Qml Qt6::SerialPort Qt6::Network Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_active_master_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME active_master COMMAND modbuslens_active_master_tests)

    # M10-D3: the atomic FC06 confirm+dispatch suite (R1-R5, the guard matrix,
    # the guard-vs-transport-NotSent distinction, evidence/terminal integration,
    # statistics/diagnosis universes, capability metadata). Same harness shape
    # as active_master: the REAL controller over the deterministic recording
    # transport, no COM port and no sleep.
    add_executable(modbuslens_write_dispatch_tests
        tests/test_write_dispatch.cpp
        tests/fake_serial_transport.h
        tests/fake_serial_transport.cpp
        src/ui/AnalysisController.h
        src/ui/AnalysisController.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp
        src/ui/profile/ProfileController.h
        src/ui/profile/ProfileController.cpp
        src/ui/profile/ActiveProfileController.h
        src/ui/profile/ActiveProfileController.cpp
        src/ui/TransactionListModel.h
        src/ui/TransactionListModel.cpp
        src/ui/serial/SerialTransport.h
        src/ui/serial/SerialTransport.cpp
        src/ui/serial/SerialPortAdapter.h
        src/ui/serial/SerialPortAdapter.cpp
        src/ui/ai/ModelScopeDiagnosisClient.h
        src/ui/ai/ModelScopeDiagnosisClient.cpp
        src/ui/ai/DiagnosisPromptBuilder.h
        src/ui/ai/DiagnosisPromptBuilder.cpp
        src/ui/agent/AgentToolContext.h
        src/ui/agent/AgentToolContext.cpp
        src/ui/agent/AgentTools.h
        src/ui/agent/AgentTools.cpp
        src/ui/agent/AgentPromptBuilder.h
        src/ui/agent/AgentPromptBuilder.cpp
        src/ui/agent/ModelScopeAgentClient.h
        src/ui/agent/ModelScopeAgentClient.cpp
        src/ui/agent/AgentRuntime.h
        src/ui/agent/AgentRuntime.cpp)
    target_include_directories(modbuslens_write_dispatch_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_compile_definitions(modbuslens_write_dispatch_tests PRIVATE
        MODBUSLENS_DEMO_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/demo_v1.mlog")
    target_link_libraries(modbuslens_write_dispatch_tests PRIVATE
        modbuslens_core Qt6::Qml Qt6::SerialPort Qt6::Network Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_write_dispatch_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME write_dispatch COMMAND modbuslens_write_dispatch_tests)

    add_executable(modbuslens_serial_adapter_tests
        tests/test_serial_adapter.cpp
        src/ui/serial/SerialTransport.h
        src/ui/serial/SerialTransport.cpp
        src/ui/serial/SerialPortAdapter.h
        src/ui/serial/SerialPortAdapter.cpp)
    target_include_directories(modbuslens_serial_adapter_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(modbuslens_serial_adapter_tests PRIVATE
        modbuslens_core Qt6::SerialPort Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_serial_adapter_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME serial_adapter COMMAND modbuslens_serial_adapter_tests)

    # Diagnosis (T011 Part A): deterministic rule-based baseline tests.
    add_executable(modbuslens_diagnosis_tests tests/test_diagnosis.cpp)
    target_link_libraries(modbuslens_diagnosis_tests PRIVATE modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_diagnosis_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME diagnosis COMMAND modbuslens_diagnosis_tests)

    # AI (T011 Part B): prompt builder + ModelScope client against a local
    # fake Chat Completions endpoint — never the public internet.
    add_executable(modbuslens_ai_tests
        tests/test_ai_client.cpp
        tests/fake_chat_completions_server.cpp
        src/ui/ai/ModelScopeDiagnosisClient.h
        src/ui/ai/ModelScopeDiagnosisClient.cpp
        src/ui/ai/DiagnosisPromptBuilder.h
        src/ui/ai/DiagnosisPromptBuilder.cpp)
    target_include_directories(modbuslens_ai_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(modbuslens_ai_tests PRIVATE
        modbuslens_core Qt6::Network Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_ai_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME ai_client COMMAND modbuslens_ai_tests)

    # Agent tools (T012 Part A): read-only tool layer — offline, zero
    # network, zero provider; explicit dispatcher + deterministic snapshots
    # (QtCore JSON exists only at the arguments/serialization boundary).
    add_executable(modbuslens_agent_tools_tests
        tests/test_agent_tools.cpp
        src/ui/agent/AgentToolContext.h
        src/ui/agent/AgentToolContext.cpp
        src/ui/agent/AgentTools.h
        src/ui/agent/AgentTools.cpp)
    target_include_directories(modbuslens_agent_tools_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(modbuslens_agent_tools_tests PRIVATE
        modbuslens_core Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_agent_tools_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME agent_tools COMMAND modbuslens_agent_tools_tests)

    # Agent runtime (T012 Part B Phase 1): native tool-calling FSM +
    # provider adapter, verified against the localhost fake server only.
    add_executable(modbuslens_agent_runtime_tests
        tests/test_agent_runtime.cpp
        tests/fake_chat_completions_server.cpp
        src/ui/agent/AgentToolContext.h
        src/ui/agent/AgentToolContext.cpp
        src/ui/agent/AgentTools.h
        src/ui/agent/AgentTools.cpp
        src/ui/agent/AgentPromptBuilder.h
        src/ui/agent/AgentPromptBuilder.cpp
        src/ui/agent/ModelScopeAgentClient.h
        src/ui/agent/ModelScopeAgentClient.cpp
        src/ui/agent/AgentRuntime.h
        src/ui/agent/AgentRuntime.cpp)
    target_include_directories(modbuslens_agent_runtime_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(modbuslens_agent_runtime_tests PRIVATE
        modbuslens_core Qt6::Network Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_agent_runtime_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME agent_runtime COMMAND modbuslens_agent_runtime_tests)

    # Agent Controller integration (T012 Part B Phase 2): full Controller +
    # AgentRuntime + T011 pipeline over the localhost fake server.
    add_executable(modbuslens_agent_integration_tests
        tests/test_agent_integration.cpp
        tests/fake_chat_completions_server.cpp
        src/ui/AnalysisController.h
        src/ui/AnalysisController.cpp
        src/ui/profile/ProfileStore.h
        src/ui/profile/ProfileStore.cpp
        src/ui/profile/ProfileController.h
        src/ui/profile/ProfileController.cpp
        src/ui/profile/ActiveProfileController.h
        src/ui/profile/ActiveProfileController.cpp
        src/ui/TransactionListModel.h
        src/ui/TransactionListModel.cpp
        src/ui/serial/SerialTransport.h
        src/ui/serial/SerialTransport.cpp
        src/ui/serial/SerialPortAdapter.h
        src/ui/serial/SerialPortAdapter.cpp
        src/ui/ai/ModelScopeDiagnosisClient.h
        src/ui/ai/ModelScopeDiagnosisClient.cpp
        src/ui/ai/DiagnosisPromptBuilder.h
        src/ui/ai/DiagnosisPromptBuilder.cpp
        src/ui/agent/AgentToolContext.h
        src/ui/agent/AgentToolContext.cpp
        src/ui/agent/AgentTools.h
        src/ui/agent/AgentTools.cpp
        src/ui/agent/AgentPromptBuilder.h
        src/ui/agent/AgentPromptBuilder.cpp
        src/ui/agent/ModelScopeAgentClient.h
        src/ui/agent/ModelScopeAgentClient.cpp
        src/ui/agent/AgentRuntime.h
        src/ui/agent/AgentRuntime.cpp)
    target_include_directories(modbuslens_agent_integration_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_compile_definitions(modbuslens_agent_integration_tests PRIVATE
        MODBUSLENS_DEMO_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/demo_v1.mlog")
    target_link_libraries(modbuslens_agent_integration_tests PRIVATE
        modbuslens_core Qt6::Qml Qt6::SerialPort Qt6::Network Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_agent_integration_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME agent_integration COMMAND modbuslens_agent_integration_tests)

    # QML load smoke: runs the REAL app executable in diagnostic mode — it
    # loads the actual shipped QML module, verifies instantiation, and exits
    # without entering the event loop (T008 Part A rule A).
    add_test(NAME qml_smoke
        COMMAND modbuslens --qml-smoke-test)

    # M9-A statistics-migration geometry guard (ISSUE-012): the real app
    # loads the shipped QML and asserts the layout-size contract of the
    # statistics area at the default size AND at the 1000x700 minimum.
    add_test(NAME qml_geometry_check
        COMMAND modbuslens --qml-geometry-check)

    # M9-B2 navigation guard: two real workspaces, page identity, visibility
    # and "navigation changes no business state" (ISSUE-012 family: the
    # guard layer keeps growing only where a real contract exists).
    add_test(NAME qml_nav_check
        COMMAND modbuslens --qml-nav-check)

    # M9-F F1 focus / keyboard-traversal guard: workspace focus gating (a
    # hidden page must neither acquire nor keep consumable focus), rail
    # keyboard activation and accessible identity, keyboard-only entry into
    # the Transactions list, and the Agent TextArea traversal contract.
    # Same architecture as qml_nav_check: the real app asserts its own
    # shipped QML through the existing synthetic event seams.
    add_test(NAME qml_focus_check
        COMMAND modbuslens --qml-focus-check)

    # M10-C2 hidden write-foundation guard: the harness-only visibility seam
    # instantiates the hidden write UI (drafts / validation presentation /
    # confirmation dialog) and asserts the C2 oracle set — including that the
    # transport is never touched (no 0x06 / 0x10 capability exists).
    add_test(NAME qml_write_foundation_check
        COMMAND modbuslens --qml-write-foundation-check)

    # M10-D4 production-write guard: the SHIPPED production write section (FC06
    # only) is present because of the structural capability, and its real
    # Confirm button performs the atomic confirm+dispatch. Asserts the send
    # oracles (Space / Enter / rapid x2 / immediate Enter), Cancel / Escape,
    # modal safety, the non-success presentation lane (not sent / short
    # submission / write timeout) and the 1024x720 + 1000x700 geometry.
    add_test(NAME qml_production_write_check
        COMMAND modbuslens --qml-production-write-check)

    # T023 / M10 correction read-result guard: the FC03 read-result surface
    # (the Human discovery — a successful read said only 「成功」). Drives the
    # SHIPPED QML with a harness transport that supplies exact observed bytes
    # and asserts the summary/terminal states, the TX==preview==encoder
    # identity, byte-for-byte RX fidelity, the CLASS-10 raw uint16 table, the
    # frozen no-byte/disposition wording, the reachable bounded evidence
    # dialog, and the 1000x700 geometry (incl. the 125-register table) that
    # keeps the write panel inside the window.
    add_test(NAME qml_read_result_check
        COMMAND modbuslens --qml-read-result-check)

    # M11 first-slice Human visual demo (TEST-ONLY / DEMO-ONLY): the demo
    # harness runs the same production read path with a FIXED synthetic FC03
    # success (values 0x1234 / 0xFFFF / 0x0080) and asserts the read-result
    # projection + decode defaults via DEMODECODE READY. The --demo-exit-
    # after-ready flag ends the process after the assertions so ctest can run
    # it headless; without it the demo keeps the GUI open for Human.
    add_test(NAME qml_read_result_demo
        COMMAND modbuslens --qml-read-result-demo --demo-exit-after-ready)

    # M11 second-slice demo gate: the 32-bit views (UInt32 / Int32 / Float32 +
    # word order) driven through the SAME production path, with DEMODECODE32
    # assertions on the projected rows at every stage (sliding window, spans,
    # last-register insufficient_words, raw preservation, order flip). Same
    # headless contract as the first-slice demo above.
    add_test(NAME qml_read_result_demo32
        COMMAND modbuslens --qml-read-result-demo32 --demo-exit-after-ready)

    # M12-B first slice gate: the Device Profile workspace (rail index 5)
    # driven end to end against an injected temporary managed root.
    add_test(NAME qml_profile_editor_check
        COMMAND modbuslens --qml-profile-editor-check)

    # M12-B second slice gate: the Register Map editor (rail → workspace →
    # profile → register map UI → entry editor dialog) end to end against an
    # injected temporary managed root.
    add_test(NAME qml_register_map_check
        COMMAND modbuslens --qml-register-map-check)

    # M12-B third slice gate: the Communication workspace's lightweight
    # current-profile selector (session Active Profile) driven end to end
    # against an injected temporary managed root.
    add_test(NAME qml_active_profile_check
        COMMAND modbuslens --qml-active-profile-check)

    # M12-C C1a gate: the Manual Import area inside the Device Profile
    # workspace (import TXT/Markdown, document list, detail, plain-text
    # preview, no AI/Candidate/Q&A controls) driven end to end against an
    # injected temporary managed root.
    add_test(NAME qml_manual_import_check
             COMMAND modbuslens --qml-manual-import-check)

    # M12-C C2 third-slice REMEDIATION (Session O-R1, T027 §72): the cloud
    # consent dialog must stay contained at the Human-reported 1280x937
    # viewport, its disclosure text must wrap inside the dialog, and Cancel /
    # Agree must be reachable by a REAL mouse interaction. This gate drives the
    # shipped QML through the shipped buttons; it never calls a controller.
    add_test(NAME qml_consent_check
             COMMAND modbuslens --qml-consent-check)

    # Same rule as the Manual Import windows entry: re-run the ONE consent-gate
    # implementation on the real platform, because the Human failure was
    # observed on the real Windows QPA. The platform is pinned explicitly so an
    # inherited offscreen value can never silently turn this into an offscreen
    # run.
    add_test(NAME qml_consent_check_windows
        COMMAND modbuslens --qml-consent-check)

    # SESSION P-R1C (T027 §78): the base consent gate drives a GUI-subsystem
    # exe whose Qt diagnostics go NOWHERE when stderr is a pipe and no console
    # is assumed — its first real failure therefore surfaced as a silent,
    # output-less ctest FAIL that read like a crash. Declaring the stderr
    # contract makes every future failure diagnosable. The PLATFORM stays
    # deliberately unpinned exactly as accepted in O-R1; only the stderr
    # contract is added.
    set_tests_properties(qml_consent_check PROPERTIES
        ENVIRONMENT "QT_ASSUME_STDERR_HAS_CONSOLE=1"
    )

    # All QML gates above run under QT_QPA_PLATFORM=offscreen so they stay
    # headless-safe. Offscreen, however, does NOT use the shipped font
    # metrics: measured on the same binary, a line of text is 12px offscreen
    # and 16px on the real Windows platform, and that difference accumulates
    # down a stacked page. The C4 write-section geometry contract (panel and
    # validation message fully inside the window at 1000x700) therefore passed
    # offscreen while failing on the platform the product actually ships on.
    # This one extra entry re-runs the write-section geometry under the real
    # windows QPA so a "the write panel no longer fits 1000x700" regression is
    # caught here instead of for the first time in a portable run.
    # Requires an interactive Windows session (a desktop is present).
    add_test(NAME qml_write_foundation_check_windows
        COMMAND modbuslens --qml-write-foundation-check)

    # M12-C C1a: the SAME blind spot, second instance. The Manual Import area was
    # measured green offscreen while overflowing the window under the real
    # windows font metrics (manualDocumentList / manualPreview ended at y=707 in
    # a 700px-high window). This entry re-runs the SAME production check
    # (--qml-manual-import-check, no second implementation) on the real platform
    # so the 1000x700 contract is gated where the product actually ships.
    # Unlike the write-foundation entry above it also pins QT_QPA_PLATFORM=windows
    # explicitly, so an inherited offscreen value in the environment can never
    # silently turn this gate back into an offscreen run.
    # Requires an interactive Windows session (a desktop is present).
    add_test(NAME qml_manual_import_check_windows
        COMMAND modbuslens --qml-manual-import-check)

    # M9-B4.3 scenario fixtures (repo-relative: copied into the build tree,
    # same mechanism as the demo_v1 test_data above -- no absolute paths).
    target_compile_definitions(modbuslens PRIVATE
        MODBUSLENS_DEMO_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/demo_v1.mlog"
        MODBUSLENS_UNSUPPORTED_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/t015_unsupported_fc08.mlog"
        MODBUSLENS_BROADCAST_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/t015_broadcast.mlog"
        MODBUSLENS_PROTOCOL_ERROR_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/t014_protocol_error.mlog")

    # Headless-safe on every platform (no display server needed).
    #
    # QT_ASSUME_STDERR_HAS_CONSOLE=1 forces Qt to write its diagnostics to the
    # inherited stderr instead of OutputDebugString. The app is a WIN32
    # subsystem binary: without this flag its QML warnings go to the debugger
    # and CTest captures an EMPTY Output block, so a defect that only shows up
    # in the diagnostics text is invisible here while it still fails the
    # shipping gate (scripts/make_package.py minimal_path_run greps the
    # captured stdout+stderr of the extracted package).
    set_tests_properties(crc frame codec f03 simulator simulator_integration fault fault_integration transaction transaction_integration statistics statistics_integration ui_bridge register_decode device_profile profile_controller active_profile_controller manual_import profile_semantic replay_log replay_analysis serial serial_adapter active_request active_master write_prepare write_dispatch diagnosis ai_client agent_tools agent_runtime agent_integration qml_smoke qml_geometry_check qml_nav_check qml_focus_check qml_write_foundation_check qml_production_write_check qml_read_result_check qml_read_result_demo qml_read_result_demo32 qml_profile_editor_check qml_register_map_check qml_active_profile_check qml_manual_import_check qml_profile_semantic_check qml_profile_semantic_demo PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_ASSUME_STDERR_HAS_CONSOLE=1"
    )

    # The Windows-layout entry deliberately does NOT get the offscreen
    # environment above: its whole purpose is the real platform's font metrics.
    set_tests_properties(qml_write_foundation_check_windows PROPERTIES
        ENVIRONMENT "QT_ASSUME_STDERR_HAS_CONSOLE=1"
    )

    # Same rule for the Manual Import windows entry, but with the platform stated
    # explicitly instead of relying on the ambient environment.
    set_tests_properties(qml_manual_import_check_windows PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_ASSUME_STDERR_HAS_CONSOLE=1"
    )

    # Same for the consent-gate windows entry (Session O-R1): the Human failure
    # was seen on the real Windows QPA, so that is where it must be re-gated.
    set_tests_properties(qml_consent_check_windows PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_ASSUME_STDERR_HAS_CONSOLE=1"
    )

    # Source-tree mirror of the shipping warning rejection, applied to EVERY QML
    # diagnostic mode in the acceptance set. A QML binding that cannot resolve an
    # identifier, or that reads an absent map key / an invalidated model index,
    # evaluates to undefined and is reported only as a runtime diagnostic -- the
    # process still exits 0 -- so an exit-code-only test asserts nothing about it
    # (the packaging gate is the only place that greps the text).
    #   ReferenceError / TypeError : scope and binding failures
    #   Unable to assign           : a failed binding assigning undefined to a
    #                                typed property (absent map key, or a role
    #                                read against an invalidated model index)
    set_tests_properties(qml_smoke qml_nav_check qml_geometry_check qml_focus_check qml_write_foundation_check qml_production_write_check qml_read_result_check qml_read_result_demo qml_read_result_demo32 qml_profile_editor_check qml_register_map_check qml_active_profile_check qml_manual_import_check qml_manual_import_check_windows qml_consent_check qml_consent_check_windows qml_profile_semantic_check qml_profile_semantic_demo qml_write_foundation_check_windows PROPERTIES
        FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;Unable to assign;String.arg..: Invalid arguments"
    )
endif()
# ---------------------------------------------------------------------------
# M12-C C2 SESSION P-R1 (T027 §77) -- deterministic Windows test runtime.
#
# A Windows test process resolves its runtime DLLs from PATH, and an ambient
# developer PATH can resolve libstdc++-6.dll / libgcc_s_seh-1.dll /
# libwinpthread-1.dll from a FOREIGN MinGW toolchain before the configured kit
# (measured: a 2018-era libstdc++-6.dll that does not export the
# _ZSt28__throw_bad_array_new_lengthv entry point the tests require, which made
# every CTest test fail with 0xc0000139 STATUS_ENTRYPOINT_NOT_FOUND). CTest
# therefore owns runtime identity: the runtime directories DERIVED from this
# build's configure-time truth are PREPENDED, so they always win, while the
# inherited PATH remains afterwards for non-runtime tools.
#
#   Qt runtime bin   <- Qt6_DIR reported by find_package(Qt6)
#   MinGW runtime bin <- parent directory of CMAKE_CXX_COMPILER
#
# No machine-specific absolute path is recorded in this file, and ambient PATH
# is never searched to decide runtime identity.
# ---------------------------------------------------------------------------
if(WIN32)
    get_filename_component(MODBUSLENS_QT_RUNTIME_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    set(MODBUSLENS_QT_RUNTIME_BIN "${MODBUSLENS_QT_RUNTIME_PREFIX}/bin")
    get_filename_component(MODBUSLENS_MINGW_RUNTIME_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)

    get_property(MODBUSLENS_ALL_TESTS DIRECTORY PROPERTY TESTS)
    if(MODBUSLENS_ALL_TESTS)
        set_tests_properties(${MODBUSLENS_ALL_TESTS} PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MODBUSLENS_MINGW_RUNTIME_BIN};PATH=path_list_prepend:${MODBUSLENS_QT_RUNTIME_BIN}")
    endif()
endif()
