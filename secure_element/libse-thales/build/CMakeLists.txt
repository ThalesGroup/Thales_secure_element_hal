cmake_minimum_required(VERSION 3.10)

project(libse_thales)

# Configuration options
option(LEGACY "Use legacy middleware(gp by default)" OFF)
option(MULTI_THREADING "Use mutex for multi-client (set by default)" ON)
option(IRQ_MODE "Enable interruption IRQ mode" OFF)
option(ENABLE_LOGGING "Enable system logging" ON)
option(ENABLE_DEBUG "Enable debug messages" ON)
option(ENABLE_GCOV "Enable GCC coverage support" OFF)


if(LEGACY)
    project(libse-thales VERSION 1.9.0 LANGUAGES C CXX)
    configure_file(
        "${CMAKE_SOURCE_DIR}/libse-thales_version.h.in"
        "${CMAKE_SOURCE_DIR}/../legacy/se-thales/libse-thales_version.h"
        @ONLY
    )
else()
    project(libse-thales VERSION 2.1.0 LANGUAGES C CXX)
    configure_file(
        "${CMAKE_SOURCE_DIR}/libse-thales_version.h.in"
        "${CMAKE_SOURCE_DIR}/../gp/se-thales/libse-thales_version.h"
        @ONLY
    )
endif()

# Library sources
if(LEGACY)
    set(LIBSE_THALES_SOURCES
        ${CMAKE_SOURCE_DIR}/../legacy/src/api/libse-thales.c
        ${CMAKE_SOURCE_DIR}/../legacy/src/core/checksum.c
        ${CMAKE_SOURCE_DIR}/../legacy/src/core/iso7816_t1.c
        ${CMAKE_SOURCE_DIR}/../legacy/src/core/log.c
        ${CMAKE_SOURCE_DIR}/../legacy/src/driver/spi/transport.c
        ${CMAKE_SOURCE_DIR}/../legacy/src/driver/spi/spi.c
    )
else()
    set(LIBSE_THALES_SOURCES
        ${CMAKE_SOURCE_DIR}/../gp/src/api/libse-thales.c
        ${CMAKE_SOURCE_DIR}/../gp/src/core/checksum.c
        ${CMAKE_SOURCE_DIR}/../gp/src/core/gpio_core.c
        ${CMAKE_SOURCE_DIR}/../gp/src/core/iso7816_t1.c
        ${CMAKE_SOURCE_DIR}/../gp/src/core/log.c
        ${CMAKE_SOURCE_DIR}/../gp/src/driver/spi/spi.c
        ${CMAKE_SOURCE_DIR}/../gp/src/driver/spi/transport.c
    )
endif()

# Create shared library
add_library(se-thales STATIC ${LIBSE_THALES_SOURCES})

# Library version (libtool format: current:revision:age)
# CMake uses current-age.age.revision
set_target_properties(se-thales PROPERTIES
    OUTPUT_NAME se-thales
)

# Compile definitions
target_compile_definitions(se-thales PRIVATE
    SYSCONFDIR="${CMAKE_INSTALL_FULL_SYSCONFDIR}"
    LIBEXECDIR="${CMAKE_INSTALL_FULL_LIBEXECDIR}"
)

if(LEGACY)
    # Include directories
    target_include_directories(se-thales
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/../legacy/se-thales>
            $<INSTALL_INTERFACE:include/libse-thales>
        PRIVATE
            ${CMAKE_SOURCE_DIR}/../legacy/include/core
            ${CMAKE_SOURCE_DIR}/../legacy/include/driver
            ${CMAKE_SOURCE_DIR}/../legacy/include/driver/spi
            ${CMAKE_SOURCE_DIR}/../legacy/include/linux
            ${CMAKE_SOURCE_DIR}/../legacy
            ${CMAKE_BINARY_DIR}
    )
else()
    # Include directories
    target_include_directories(se-thales
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/../gp/se-thales>
            $<INSTALL_INTERFACE:include/libse-thales>
        PRIVATE
            ${CMAKE_SOURCE_DIR}/../gp/include/core
            ${CMAKE_SOURCE_DIR}/../gp/include/driver
            ${CMAKE_SOURCE_DIR}/../gp/include/driver/spi
            ${CMAKE_SOURCE_DIR}/../gp/include/linux
            ${CMAKE_SOURCE_DIR}/../gp
            ${CMAKE_BINARY_DIR}
    )
endif()

# Link libraries
target_link_libraries(se-thales
    PRIVATE
        rt
        pthread
)

# Installation
install(TARGETS se-thales
    EXPORT libse-thalesTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install public header
install(FILES se-thales/libse-thales.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libse-thales
)

# Export targets for find_package
install(EXPORT libse-thalesTargets
    FILE libse-thalesTargets.cmake
    NAMESPACE libse-thales::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libse-thales
)

# Create config file for find_package
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libse-thalesConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/libse-thalesConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libse-thalesConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libse-thales
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libse-thalesConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libse-thalesConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libse-thales
)

# pkg-config file
configure_file(
    ${CMAKE_SOURCE_DIR}/../libse-thales.pc.in
    ${CMAKE_SOURCE_DIR}/../libse-thales.pc
)

# Print configuration summary
message(STATUS "")
message(STATUS "${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "====================================")
message(STATUS "")
message(STATUS "logging:                ${ENABLE_LOGGING}")
message(STATUS "legacy:                 ${LEGACY}")
message(STATUS "multi-client:           ${MULTI_THREADING}")
message(STATUS "debug:                  ${ENABLE_DEBUG}")
message(STATUS "irq:                    ${IRQ_MODE}")
message(STATUS "gcov coverage:          ${ENABLE_GCOV}")
message(STATUS "se device node:         ${DEVNODE}")
message(STATUS "")

add_custom_target(clean_lib
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/CMakeCache.txt
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/cmake_install.cmake
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/Makefile
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/libse-thalesConfig.cmake
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/libse-thalesConfigVersion.cmake
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/libse-thales.a
    COMMENT "Cleaning cmake file generated"
)