cmake_minimum_required(VERSION 3.22)
project(quantum-circuit-simulator LANGUAGES C CXX) # CXX: Dawn, via the engine

option(QCS_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(QCS_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

set(FETCHCONTENT_QUIET FALSE)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# The engine, a sibling checkout by default. Its examples and tests stay off:
# it isn't the top-level project here.
set(QCS_GFX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../allyria-gfx
    CACHE PATH "allyria-gfx checkout")
add_subdirectory(${QCS_GFX_DIR} allyria-gfx)

# Sanitizers for this repo's targets, added after the engine so it stays
# uninstrumented
if(QCS_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# compile_commands.json for editors (clangd), this repo's files only
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# QCS library, linked by the CLI, the tests and the viewer
set(QCS_SOURCES
    ./lib/qcs_core.c
    ./lib/qcs_gate.c
    ./lib/qcs_circuit.c
    ./lib/qcs_state.c
    ./lib/qcs_random.c
    ./lib/qcs_gpu.c
    ./lib/qcs_program.c
    ./lib/qcs_simulator.c
    ./lib/qasm/qcs_qasm_lexer.c
    ./lib/qasm/qcs_qasm_ast.c
    ./lib/qasm/qcs_qasm_parser.c
    ./lib/qasm/qcs_qasm_program.c
)
add_library(qcs ${QCS_SOURCES})
target_compile_features(qcs PUBLIC c_std_23)
set_target_properties(qcs PROPERTIES C_EXTENSIONS OFF)
target_include_directories(qcs
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
# gfx is PUBLIC: GPU state in qcs.h will hold engine types
target_link_libraries(qcs PUBLIC gfx PRIVATE stb)
# libm for cexp, sqrt, ... (part of libc on Apple)
if(UNIX AND NOT APPLE)
  target_link_libraries(qcs PUBLIC m)
endif()
# Shaders load from the source tree, so edits apply on the next run
target_compile_definitions(qcs PRIVATE
    QCS_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders/")

# CLI, built as ./build/qcs (the target name qcs is the library's)
add_executable(qcs_cli src/main.c src/args.c)
set_target_properties(qcs_cli PROPERTIES OUTPUT_NAME qcs C_EXTENSIONS OFF)
target_link_libraries(qcs_cli PRIVATE qcs)

# Tests, run with: ctest --test-dir build -L cpu (or -L gpu)
if(QCS_BUILD_TESTS)
  enable_testing()
  # qcs_test(test_gates cpu) builds tests/test_gates.c, labelled cpu or gpu
  function(qcs_test name label)
    add_executable(${name} tests/${name}.c)
    set_target_properties(${name} PROPERTIES C_EXTENSIONS OFF)
    # Tests may check library internals
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
    target_link_libraries(${name} PRIVATE qcs)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS ${label})
  endfunction()

  # One line per test file
  qcs_test(test_core cpu)
  qcs_test(test_gates cpu)
  qcs_test(test_circuit cpu)
  qcs_test(test_state cpu)
  qcs_test(test_program cpu)
  qcs_test(test_qasm_lexer cpu)
  qcs_test(test_qasm_ast cpu)
  qcs_test(test_qasm_parser cpu)
  qcs_test(test_qasm_program cpu)
  qcs_test(test_gpu gpu)

  # Benchmark, not a test: ./build/bench cpu|gpu [max_qubits]
  add_executable(bench bench/bench.c)
  set_target_properties(bench PROPERTIES C_EXTENSIONS OFF)
  target_link_libraries(bench PRIVATE qcs)
endif()
