cmake_minimum_required(VERSION 3.10)

project(libse_thales_and_tools)
# Configuration options
option(I3C "Use I3C communication (SPI by default)" OFF)
option(LEGACY "Use legacy middleware(gp by default)" OFF)
option(MULTI_THREADING "Use mutex for multi-client (set by default)" ON)
option(IRQ_MODE "Enable interruption IRQ mode" OFF)
option(ENABLE_LOGGING "Enable system logging" ON)
option(ENABLE_DEBUG "Enable debug messages" ON)
option(ENABLE_GCOV "Enable GCC coverage support" OFF)
option(BUILD_TOOLS "Build thales-se-tool executable" OFF)
option(BUILD_TESTS "Build unit tests" OFF)

if(LEGACY)
option(IRQ_MODE "Enable interruption IRQ mode" OFF)
endif()

# Device node configuration
set(DEVNODE "/dev/node" CACHE STRING "SE Thales device node name")

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wchar-subscripts")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wformat-security")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-declarations")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-prototypes")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wnested-externs")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wpointer-arith")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wshadow")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wsign-compare")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wstrict-prototypes")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wtype-limits")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fdata-sections")

# GCov support
if(ENABLE_GCOV)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 --no-inline -fprofile-arcs -ftest-coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lgcov")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -lgcov")
endif()

# Install directories
include(GNUInstallDirs)

# Subdirectories
add_subdirectory(libse-thales/build)

# pkg-config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/libse-thales/libse-thales.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/libse-thales.pc
)

# Print configuration summary
message(STATUS "")
message(STATUS "=================")
message(STATUS "${PROJECT_NAME}")
message(STATUS "=================")
message(STATUS "")
message(STATUS "prefix:                 ${CMAKE_INSTALL_PREFIX}")
message(STATUS "sysconfdir:             ${CMAKE_INSTALL_FULL_SYSCONFDIR}")
message(STATUS "libdir:                 ${CMAKE_INSTALL_FULL_LIBDIR}")
message(STATUS "includedir:             ${CMAKE_INSTALL_FULL_INCLUDEDIR}")
message(STATUS "")
message(STATUS "compiler:               ${CMAKE_C_COMPILER}")
message(STATUS "cflags:                 ${CMAKE_C_FLAGS}")
message(STATUS "ldflags:                ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "")
message(STATUS "build tools:            ${BUILD_TOOLS}")
message(STATUS "build tests:            ${BUILD_TESTS}")
message(STATUS "")
