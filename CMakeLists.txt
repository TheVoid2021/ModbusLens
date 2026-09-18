cmake_minimum_required(VERSION 3.21)

project(ModbusLens
    VERSION 0.1.0
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
    src/core/analysis/PassiveTransactionAnalysis.cpp
    src/core/analysis/TransactionStatistics.cpp
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
qt_add_executable(modbuslens WIN32
    src/main.cpp
)
# Note on DesignSystem.qml (M9-A): it is deliberately a PLAIN QML file (no
# pragma Singleton). The module-singleton route (pragma Singleton +
# QT_QML_SINGLETON_TYPE) compiled and generated the qmldir correctly but
# still failed at runtime with "DS is not defined" (ISSUE-010); the tokens
# are therefore exposed via an engine context property created in main.cpp.
qt_add_qml_module(modbuslens
    URI ModbusLens
    VERSION 1.0
    QML_FILES
        src/ui/qml/Main.qml
        src/ui/qml/DS/DesignSystem.qml
        src/ui/qml/components/AppButton.qml
        src/ui/qml/components/NavigationRail.qml
        src/ui/qml/components/PanelCard.qml
        src/ui/qml/components/SectionHeader.qml
        src/ui/qml/components/StatCard.qml
        src/ui/qml/components/StatisticsMetrics.qml
        src/ui/qml/components/StatisticsOutcomes.qml
        src/ui/qml/components/OutcomeDistribution.qml
        src/ui/qml/components/StatisticsOverview.qml
        src/ui/qml/pages/DashboardPage.qml
        src/ui/qml/pages/CommunicationPage.qml
        src/ui/qml/pages/ReplayPage.qml
        src/ui/qml/pages/DiagnosisPage.qml
        src/ui/qml/pages/TransactionsPage.qml
    SOURCES
        src/ui/AnalysisController.h
        src/ui/AnalysisController.cpp
        src/ui/TransactionListModel.h
        src/ui/TransactionListModel.cpp
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
target_link_libraries(modbuslens PRIVATE
    modbuslens_core Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::SerialPort Qt6::Network)

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
        src/ui/AnalysisController.h
        src/ui/AnalysisController.cpp
        src/ui/TransactionListModel.h
        src/ui/TransactionListModel.cpp
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
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
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

    add_executable(modbuslens_serial_adapter_tests
        tests/test_serial_adapter.cpp
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
        src/ui/TransactionListModel.h
        src/ui/TransactionListModel.cpp
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

    # M9-B4.3 scenario fixtures (repo-relative: copied into the build tree,
    # same mechanism as the demo_v1 test_data above -- no absolute paths).
    target_compile_definitions(modbuslens PRIVATE
        MODBUSLENS_DEMO_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/demo_v1.mlog"
        MODBUSLENS_UNSUPPORTED_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/t015_unsupported_fc08.mlog"
        MODBUSLENS_BROADCAST_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/t015_broadcast.mlog"
        MODBUSLENS_PROTOCOL_ERROR_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/t014_protocol_error.mlog")

    # Headless-safe on every platform (no display server needed).
    set_tests_properties(crc frame codec f03 simulator simulator_integration fault fault_integration transaction transaction_integration statistics statistics_integration ui_bridge replay_log replay_analysis serial serial_adapter diagnosis ai_client agent_tools agent_runtime agent_integration qml_smoke qml_geometry_check qml_nav_check PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()