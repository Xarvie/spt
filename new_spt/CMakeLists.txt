# ======================================================================
#  SPT — a register-VM Lua dialect with an optional MIR JIT.
#
#  Key build switches:
#    -DSPT_JIT=ON|OFF      Build the MIR-backed JIT. Default ON, except on
#                          iOS where it is forced OFF (Apple forbids JIT).
#                          The interpreter is ALWAYS built and is authoritative.
#    -DSPT_MIR_DIR=<path>  Location of MIR sources (default: vendored copy).
#
#  Interpreter-only (e.g. iOS) build:
#    cmake -B build -DSPT_JIT=OFF && cmake --build build
#
#  JIT build (desktop/server):
#    cmake -B build -DSPT_JIT=ON  && cmake --build build
# ======================================================================
cmake_minimum_required(VERSION 3.16)
project(spt LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# We rely on the computed-goto GNU extension on GCC/Clang; conf.h falls back to
# a switch on toolchains without it (e.g. MSVC). Keeping extensions on enables it.
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- JIT availability --------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(_spt_jit_default OFF)
  message(STATUS "SPT: iOS target detected — JIT is unavailable, forcing interpreter-only.")
else()
  set(_spt_jit_default ON)
endif()
option(SPT_JIT "Build the MIR-backed JIT" ${_spt_jit_default})
set(SPT_MIR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mir"
    CACHE PATH "Path to MIR sources")

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(SPT_JIT OFF CACHE BOOL "" FORCE)   # never allow JIT on iOS
endif()

# ---- core engine (always present) -------------------------------------
add_library(spt STATIC
  src/mem.c
  src/object.c
  src/state.c
  src/vm.c
  src/api.c
  src/lexer.c
  src/parser.c
  src/codegen.c)

target_include_directories(spt
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
  target_compile_options(spt PRIVATE /W3)
else()
  target_compile_options(spt PRIVATE -Wall -Wextra
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-g>)
  target_link_libraries(spt PUBLIC m)
endif()

# ---- optional MIR JIT layer -------------------------------------------
if(SPT_JIT)
  if(NOT EXISTS "${SPT_MIR_DIR}/mir.c")
    message(FATAL_ERROR
      "SPT_JIT=ON but MIR was not found at '${SPT_MIR_DIR}'. "
      "Pass -DSPT_MIR_DIR=<path-to-mir> or place MIR under third_party/mir.")
  endif()

  # MIR is third-party C that expects GNU mode and an explicit <stdarg.h>.
  # Build it as its own target so its flags never leak into the engine.
  add_library(spt_mir STATIC ${SPT_MIR_DIR}/mir.c ${SPT_MIR_DIR}/mir-gen.c)
  set_target_properties(spt_mir PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
  target_include_directories(spt_mir PUBLIC ${SPT_MIR_DIR})
  if(NOT MSVC)
    target_compile_options(spt_mir PRIVATE -w -include stdarg.h)
    target_link_libraries(spt_mir PUBLIC ${CMAKE_DL_LIBS})
  endif()
  find_package(Threads REQUIRED)
  target_link_libraries(spt_mir PUBLIC Threads::Threads)

  target_sources(spt PRIVATE src/jit/jit_mir.c)
  target_compile_definitions(spt PUBLIC SPT_JIT)
  target_include_directories(spt PRIVATE ${SPT_MIR_DIR})
  target_link_libraries(spt PUBLIC spt_mir)
  message(STATUS "SPT: JIT ENABLED (MIR at ${SPT_MIR_DIR})")
else()
  message(STATUS "SPT: interpreter-only build (JIT disabled)")
endif()

# ---- demo executables --------------------------------------------------
add_executable(spt_demo examples/demo.c)
target_link_libraries(spt_demo PRIVATE spt)

add_executable(spt_run examples/run_source.c)
target_link_libraries(spt_run PRIVATE spt)

enable_testing()
add_test(NAME demo COMMAND spt_demo)
add_test(NAME run_source COMMAND spt_run)
