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

# ---------------------------------------------------------------- application --
add_executable(modbuslens WIN32 src/main.cpp)
target_link_libraries(modbuslens PRIVATE Qt6::Widgets)

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

    # Headless-safe on every platform (no display server needed).
    set_tests_properties(smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()