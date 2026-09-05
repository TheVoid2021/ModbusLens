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

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# ------------------------------------------------------------------- core ----
# Pure C++ protocol/analysis core. Deliberately links no Qt at all: the same
# logic must be shared by Simulator/Replay/Serial and stay unit-testable and
# reusable even if Qt is dropped (see docs/02_ARCHITECTURE.md D3).
add_library(modbuslens_core STATIC
    src/core/protocol/ModbusCrc.cpp
    src/core/protocol/ModbusRtuCodec.cpp
    src/core/protocol/Function03.cpp
)
target_include_directories(modbuslens_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(modbuslens_core PRIVATE -Wall -Wextra)
endif()

# ------------------------------------------------------------- application --
add_executable(modbuslens WIN32 src/main.cpp)
target_link_libraries(modbuslens PRIVATE Qt6::Widgets modbuslens_core)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(modbuslens PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------- tests --
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(modbuslens_tests tests/test_smoke.cpp)
    target_link_libraries(modbuslens_tests PRIVATE Qt6::Widgets Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(modbuslens_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME smoke COMMAND modbuslens_tests)

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

    # Headless-safe on every platform (no display server needed).
    set_tests_properties(smoke crc frame codec f03 PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()