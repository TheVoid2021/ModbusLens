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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 SerialPort Test)
qt_standard_project_setup()

# ------------------------------------------------------------------- core ----
# Pure C++ protocol/analysis core. Deliberately links no Qt at all: the same
# logic must be shared by Simulator/Replay/Serial and stay unit-testable and
# reusable even if Qt is dropped (see docs/02_ARCHITECTURE.md D3).
add_library(modbuslens_core STATIC
    src/core/protocol/ModbusCrc.cpp
    src/core/protocol/ModbusRtuCodec.cpp
    src/core/protocol/Function03.cpp
    src/core/simulator/SimulatedSlave.cpp
    src/core/simulator/SimulationFault.cpp
    src/core/analysis/TransactionAnalysis.cpp
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
qt_add_qml_module(modbuslens
    URI ModbusLens
    VERSION 1.0
    QML_FILES
        src/ui/qml/Main.qml
    SOURCES
        src/ui/AnalysisController.h
        src/ui/AnalysisController.cpp
        src/ui/TransactionListModel.h
        src/ui/TransactionListModel.cpp
        src/ui/serial/SerialPortAdapter.h
        src/ui/serial/SerialPortAdapter.cpp
)
target_include_directories(modbuslens PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
# T010 Part B link graph: the APP (via the serial adapter inside
# AnalysisController) links Qt6::SerialPort — modbuslens_core stays Zero Qt.
target_link_libraries(modbuslens PRIVATE
    modbuslens_core Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::SerialPort)

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
        src/ui/AnalysisController.h
        src/ui/AnalysisController.cpp
        src/ui/TransactionListModel.h
        src/ui/TransactionListModel.cpp
        src/ui/serial/SerialPortAdapter.h
        src/ui/serial/SerialPortAdapter.cpp)
    target_include_directories(modbuslens_ui_bridge_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(modbuslens_ui_bridge_tests PRIVATE
        MODBUSLENS_DEMO_MLOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test_data/demo_v1.mlog")
    target_link_libraries(modbuslens_ui_bridge_tests PRIVATE
        modbuslens_core Qt6::Qml Qt6::SerialPort Qt6::Test)

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

    # QML load smoke: runs the REAL app executable in diagnostic mode — it
    # loads the actual shipped QML module, verifies instantiation, and exits
    # without entering the event loop (T008 Part A rule A).
    add_test(NAME qml_smoke
        COMMAND modbuslens --qml-smoke-test)

    # Headless-safe on every platform (no display server needed).
    set_tests_properties(crc frame codec f03 simulator simulator_integration fault fault_integration transaction transaction_integration statistics statistics_integration ui_bridge replay_log replay_analysis serial serial_adapter diagnosis qml_smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()