cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" CATALYST_VERSION_RAW LIMIT_COUNT 1)
string(STRIP "${CATALYST_VERSION_RAW}" CATALYST_VERSION_RAW)
string(REGEX REPLACE "^v" "" CATALYST_VERSION_RAW "${CATALYST_VERSION_RAW}")
string(REGEX REPLACE "[-+].*$" "" CATALYST_VERSION "${CATALYST_VERSION_RAW}")
set(CATALYST_VERSION_FULL "${CATALYST_VERSION_RAW}")
project(catalyst VERSION ${CATALYST_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC AND NOT (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
    message(FATAL_ERROR "Catalyst does not support building with MSVC (use clang-cl or MinGW).")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Release Debug RelWithDebInfo)
endif()

include(CheckIPOSupported)
include(GNUInstallDirs)

# ── objcopy ──────────────────────────────────────────────────────────────────
find_program(OBJCOPY_EXECUTABLE objcopy)
if(NOT OBJCOPY_EXECUTABLE)
    message(FATAL_ERROR "objcopy not found. Install binutils (Linux) or use a MinGW toolchain (Windows).")
endif()

# ── NNUE ─────────────────────────────────────────────────────────────────────
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/network.txt" CATALYST_NET_NAME LIMIT_COUNT 1)
string(STRIP "${CATALYST_NET_NAME}" CATALYST_NET_NAME)

set(NNUE_BASENAME "catalyst-v2.nnue")
set(NNUE_FILE     "${CMAKE_CURRENT_SOURCE_DIR}/${NNUE_BASENAME}")
set(NNUE_DL_URL   "https://github.com/AnanyTanwar/CatalystNet/releases/latest/download/${NNUE_BASENAME}")

if(NOT EXISTS "${NNUE_FILE}")
    message(STATUS "NNUE file not found — downloading ${CATALYST_NET_NAME} ...")
    file(DOWNLOAD "${NNUE_DL_URL}" "${NNUE_FILE}"
        SHOW_PROGRESS
        STATUS DOWNLOAD_STATUS
    )
    list(GET DOWNLOAD_STATUS 0 DOWNLOAD_CODE)
    if(NOT DOWNLOAD_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download NNUE from ${NNUE_DL_URL}")
    endif()
else()
    message(STATUS "Using NNUE: ${NNUE_FILE}")
endif()

# ── Git commit hash ───────────────────────────────────────────────────────────
option(CATALYST_EMBED_COMMIT "Embed git commit hash in version string" ON)
if(CATALYST_EMBED_COMMIT)
    set(CATALYST_COMMIT_HASH "unknown")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" log -1 --pretty=format:%h
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE CATALYST_COMMIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
    message(STATUS "Commit hash: ${CATALYST_COMMIT_HASH}")
endif()

# ── Build targets ─────────────────────────────────────────────────────────────
option(BUILD_NATIVE      "Build catalyst-native  (-march=native)"        ON)
option(BUILD_AVX512VNNI  "Build catalyst-avx512vnni"                     OFF)
option(BUILD_AVX512      "Build catalyst-avx512"                         OFF)
option(BUILD_BMI2        "Build catalyst-bmi2   (AVX2 + BMI2/PEXT)"     OFF)
option(BUILD_AVX2        "Build catalyst-avx2"                           OFF)
option(BUILD_SSE41       "Build catalyst-sse41  (SSE4.1 + POPCNT)"      OFF)
option(BUILD_X86_64      "Build catalyst-x86-64 (baseline, no SIMD)"    OFF)

option(CATALYST_DATAGEN  "Enable data-generation mode (-DDATAGEN)"       OFF)
option(CATALYST_PGO_GEN  "Instrument binary for PGO (generate profiles)" OFF)
option(CATALYST_PGO_USE  "Build optimised binary using PGO profiles"     OFF)
set(CATALYST_PGO_DIR "${CMAKE_BINARY_DIR}/pgo-profiles"
    CACHE STRING "Directory for PGO profile data")

if(CATALYST_PGO_GEN AND CATALYST_PGO_USE)
    message(FATAL_ERROR "CATALYST_PGO_GEN and CATALYST_PGO_USE cannot both be ON — "
                        "run PGO_GEN first, then PGO_USE in a separate configure.")
endif()

# CATALYST_DATAGEN is independent of which arch target(s) are enabled above —
# it's an orthogonal modifier, same as in the Makefile's `make datagen`.
# e.g.: cmake -B build -DBUILD_NATIVE=ON -DCATALYST_DATAGEN=ON
if(CATALYST_DATAGEN)
    message(STATUS "Data-generation mode enabled (-DDATAGEN) for all targets built this configure")
endif()

# ── Sources ───────────────────────────────────────────────────────────────────
set(CATALYST_SOURCES
    src/main.cpp
    src/uci.cpp
    src/benchmark.cpp
    src/bitboard.cpp
    src/board.cpp
    src/datagen.cpp
    src/evaluate.cpp
    src/movegen.cpp
    src/movepick.cpp
    src/nnue.cpp
    src/search.cpp
    src/thread.cpp
    src/timeman.cpp
    src/tt.cpp
)

# ── Helper: add one build target ──────────────────────────────────────────────
function(catalyst_add_target)
    cmake_parse_arguments(ARG "" "TARGET_NAME" "ARCH_FLAGS;DEFINITIONS" ${ARGN})

    set(NNUE_OBJ_DIR  "${CMAKE_BINARY_DIR}/nnue_embed/${ARG_TARGET_NAME}")
    set(NNUE_OBJ_FILE "${NNUE_OBJ_DIR}/nnue_embed.o")

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(OBJ_FMT "pe-x86-64")
    else()
        set(OBJ_FMT "elf64-x86-64")
    endif()

    get_filename_component(NNUE_DIR  "${NNUE_FILE}" DIRECTORY)
    get_filename_component(NNUE_BASE "${NNUE_FILE}" NAME)

    add_custom_command(
        OUTPUT  "${NNUE_OBJ_FILE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${NNUE_OBJ_DIR}"
        COMMAND "${OBJCOPY_EXECUTABLE}"
                --input-target=binary
                --output-target=${OBJ_FMT}
                --binary-architecture=i386:x86-64
                "${NNUE_BASE}"
                "${NNUE_OBJ_FILE}"
        WORKING_DIRECTORY "${NNUE_DIR}"
        DEPENDS "${NNUE_FILE}"
        COMMENT "Embedding NNUE into ${ARG_TARGET_NAME}"
        VERBATIM
    )

    add_custom_target(${ARG_TARGET_NAME}_nnue DEPENDS "${NNUE_OBJ_FILE}")

    add_executable(${ARG_TARGET_NAME} ${CATALYST_SOURCES})
    add_dependencies(${ARG_TARGET_NAME} ${ARG_TARGET_NAME}_nnue)
    target_link_options(${ARG_TARGET_NAME} PRIVATE "${NNUE_OBJ_FILE}")
    target_include_directories(${ARG_TARGET_NAME} PRIVATE src)

    target_compile_options(${ARG_TARGET_NAME} PRIVATE
        -Wall -Wextra -Wshadow -Wcast-qual -Wno-sign-compare
    )

    target_compile_options(${ARG_TARGET_NAME} PRIVATE
        $<$<CONFIG:Release>:        -O3 -DNDEBUG -funroll-loops -fomit-frame-pointer
                                    -falign-functions=32 -ffunction-sections -fdata-sections>
        $<$<CONFIG:Debug>:          -O0 -g3>
        $<$<CONFIG:RelWithDebInfo>: -O2 -g -DNDEBUG>
    )

    target_compile_options(${ARG_TARGET_NAME} PRIVATE ${ARG_ARCH_FLAGS})

    target_compile_definitions(${ARG_TARGET_NAME} PRIVATE
        NNUE_EMBEDDED
        CATALYST_VERSION="${CATALYST_VERSION_FULL}"
        CATALYST_NET_NAME="${CATALYST_NET_NAME}"
    )

    if(ARG_DEFINITIONS)
        target_compile_definitions(${ARG_TARGET_NAME} PRIVATE ${ARG_DEFINITIONS})
    endif()

    if(CATALYST_EMBED_COMMIT)
        target_compile_definitions(${ARG_TARGET_NAME} PRIVATE
            CATALYST_COMMIT_HASH="${CATALYST_COMMIT_HASH}"
        )
    endif()

    if(CATALYST_DATAGEN)
        target_compile_definitions(${ARG_TARGET_NAME} PRIVATE DATAGEN)
    endif()

    find_package(Threads REQUIRED)
    target_link_libraries(${ARG_TARGET_NAME} PRIVATE Threads::Threads)
    target_compile_options(${ARG_TARGET_NAME} PRIVATE -pthread)

    check_ipo_supported(RESULT LTO_OK OUTPUT LTO_ERR)
    if(LTO_OK)
        set_target_properties(${ARG_TARGET_NAME} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
        )
    else()
        target_compile_options(${ARG_TARGET_NAME} PRIVATE $<$<CONFIG:Release>:-flto=auto>)
        target_link_options(${ARG_TARGET_NAME} PRIVATE $<$<CONFIG:Release>:-flto=auto>)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(${ARG_TARGET_NAME} PRIVATE
            -static-libgcc -static-libstdc++
            $<$<CONFIG:Release>:-Wl,--gc-sections>
        )
        find_program(LLD_PATH ld.lld)
        if(LLD_PATH)
            target_link_options(${ARG_TARGET_NAME} PRIVATE -fuse-ld=lld)
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_link_options(${ARG_TARGET_NAME} PRIVATE
            -static -static-libgcc -static-libstdc++
            -Wl,--stack,8388608
            $<$<CONFIG:Release>:-Wl,--gc-sections>
        )
    endif()

    if(CATALYST_PGO_GEN)
        target_compile_options(${ARG_TARGET_NAME} PRIVATE -fprofile-generate=${CATALYST_PGO_DIR})
        target_link_options(${ARG_TARGET_NAME} PRIVATE -fprofile-generate=${CATALYST_PGO_DIR})
    elseif(CATALYST_PGO_USE)
        target_compile_options(${ARG_TARGET_NAME} PRIVATE
            -fprofile-use=${CATALYST_PGO_DIR} -Wno-missing-profile)
        target_link_options(${ARG_TARGET_NAME} PRIVATE -fprofile-use=${CATALYST_PGO_DIR})
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_custom_command(TARGET ${ARG_TARGET_NAME} POST_BUILD
            COMMAND "${CMAKE_STRIP}" "$<TARGET_FILE:${ARG_TARGET_NAME}>"
            COMMENT "Stripping ${ARG_TARGET_NAME}"
        )
    endif()

    install(TARGETS ${ARG_TARGET_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

endfunction()

# ── native ────────────────────────────────────────────────────────────────────
if(BUILD_NATIVE)
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -march=native -E -dM -
        INPUT_FILE  /dev/null
        OUTPUT_VARIABLE _NATIVE_DEFINES
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    set(_NATIVE_DEFS "")

    string(FIND "${_NATIVE_DEFINES}" "__BMI2__"       _HAS_BMI2)
    string(FIND "${_NATIVE_DEFINES}" "__znver1"       _IS_ZEN1)
    string(FIND "${_NATIVE_DEFINES}" "__znver2"       _IS_ZEN2)
    string(FIND "${_NATIVE_DEFINES}" "__AVX512BW__"   _HAS_AVX512BW)
    string(FIND "${_NATIVE_DEFINES}" "__AVX512F__"    _HAS_AVX512F)
    string(FIND "${_NATIVE_DEFINES}" "__AVX512VNNI__" _HAS_VNNI)

    if(NOT _HAS_BMI2 EQUAL -1)
        if(_IS_ZEN1 EQUAL -1 AND _IS_ZEN2 EQUAL -1)
            list(APPEND _NATIVE_DEFS USE_PEXT)
            message(STATUS "[native] BMI2/PEXT enabled")
        else()
            message(STATUS "[native] BMI2 detected but PEXT disabled (Zen1/Zen2)")
        endif()
    endif()

    if(NOT _HAS_AVX512BW EQUAL -1 AND NOT _HAS_AVX512F EQUAL -1)
        list(APPEND _NATIVE_DEFS USE_AVX512)
        message(STATUS "[native] AVX-512 enabled")
        if(NOT _HAS_VNNI EQUAL -1)
            list(APPEND _NATIVE_DEFS USE_VNNI)
            message(STATUS "[native] AVX-512 VNNI enabled")
        endif()
    endif()

    catalyst_add_target(
        TARGET_NAME catalyst-native
        ARCH_FLAGS  -march=native
        DEFINITIONS ${_NATIVE_DEFS}
    )
endif()

# ── explicit arch targets ─────────────────────────────────────────────────────
if(BUILD_AVX512VNNI)
    catalyst_add_target(
        TARGET_NAME catalyst-avx512vnni
        ARCH_FLAGS  -march=znver4 -mtune=znver4 -mavx512f -mavx512bw -mavx512vnni -mavx512dq -mavx512vl -mbmi2
        DEFINITIONS USE_AVX512 USE_VNNI USE_PEXT
    )
endif()

if(BUILD_AVX512)
    catalyst_add_target(
        TARGET_NAME catalyst-avx512
        ARCH_FLAGS  -march=x86-64-v4 -mtune=skylake-avx512 -mavx512f -mavx512bw -mbmi2
        DEFINITIONS USE_AVX512 USE_PEXT
    )
endif()

if(BUILD_BMI2)
    catalyst_add_target(
        TARGET_NAME catalyst-bmi2
        ARCH_FLAGS  -march=haswell -mtune=haswell -mavx2 -mbmi -mbmi2 -mfma -mpopcnt
        DEFINITIONS USE_PEXT
    )
endif()

if(BUILD_AVX2)
    catalyst_add_target(
        TARGET_NAME catalyst-avx2
        ARCH_FLAGS  -march=bdver4 -mno-tbm -mno-sse4a -mno-bmi2 -mtune=znver2 -mavx2 -mpopcnt
    )
endif()

if(BUILD_SSE41)
    catalyst_add_target(
        TARGET_NAME catalyst-sse41
        ARCH_FLAGS  -march=nehalem -mtune=sandybridge -msse4.1 -mpopcnt -m64
    )
endif()

if(BUILD_X86_64)
    catalyst_add_target(
        TARGET_NAME catalyst-x86-64
        ARCH_FLAGS  -march=x86-64 -mpopcnt -m64
    )
endif()

set(_ENABLED_TARGETS "")
foreach(_T IN ITEMS native avx512vnni avx512 bmi2 avx2 sse41 x86-64)
    string(TOUPPER "${_T}" _OPT)
    string(REPLACE "-" "_" _OPT "${_OPT}")
    if(BUILD_${_OPT})
        list(APPEND _ENABLED_TARGETS "catalyst-${_T}")
    endif()
endforeach()

message(STATUS "──────────────────────────────────────────")
message(STATUS "Catalyst ${CATALYST_VERSION_FULL}  (${CMAKE_BUILD_TYPE})")
message(STATUS "  Compiler : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  NNUE     : ${CATALYST_NET_NAME}")
message(STATUS "  Datagen  : ${CATALYST_DATAGEN}")
message(STATUS "  Targets  :")
foreach(_T IN LISTS _ENABLED_TARGETS)
    message(STATUS "    • ${_T}")
endforeach()
message(STATUS "──────────────────────────────────────────")