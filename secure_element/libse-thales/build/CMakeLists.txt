


if(LEGACY)
    project(libse-thales VERSION 1.9.0 LANGUAGES C CXX)
    configure_file(
        "${CMAKE_SOURCE_DIR}/libse-thales/build/libse-thales_version.h.in"
        "${CMAKE_SOURCE_DIR}/libse-thales/legacy/se-thales/libse-thales_version.h"
        @ONLY
    )
else()
    project(libse-thales VERSION 2.1.0 LANGUAGES C CXX)
    configure_file(
        "${CMAKE_SOURCE_DIR}/libse-thales/build/libse-thales_version.h.in"
        "${CMAKE_SOURCE_DIR}/libse-thales/gp/se-thales/libse-thales_version.h"
        @ONLY
    )
endif()

# Library sources
if(LEGACY)
    set(LIBSE_THALES_SOURCES
        ${CMAKE_SOURCE_DIR}/libse-thales/legacy/src/api/libse-thales.c
        ${CMAKE_SOURCE_DIR}/libse-thales/legacy/src/core/checksum.c
        ${CMAKE_SOURCE_DIR}/libse-thales/legacy/src/core/iso7816_t1.c
        ${CMAKE_SOURCE_DIR}/libse-thales/legacy/src/core/log.c
        ${CMAKE_SOURCE_DIR}/libse-thales/legacy/src/driver/spi/transport.c
        ${CMAKE_SOURCE_DIR}/libse-thales/legacy/src/driver/spi/spi.c
    )
else()
    if(I3C)
        set(LIBSE_THALES_SOURCES
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/api/libse-thales.c
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/core/checksum.c
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/core/gpio_core.c
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/core/iso7816_t1.c
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/core/log.c
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/driver/spi/spi.c
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/driver/spi/transport.c
        )
    else()
        set(LIBSE_THALES_SOURCES
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/api/libse-thales.c
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/core/checksum.c
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/core/gpio_core.c
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/core/iso7816_t1.c
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/core/log.c
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/driver/spi/spi.c
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/src/driver/spi/transport.c
        )
    endif()
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
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/libse-thales/legacy/se-thales>
            $<INSTALL_INTERFACE:include/libse-thales>
        PRIVATE
            ${CMAKE_SOURCE_DIR}/libse-thales/legacy/include/core
            ${CMAKE_SOURCE_DIR}/libse-thales/legacy/include/driver
            ${CMAKE_SOURCE_DIR}/libse-thales/legacy/include/driver/spi
            ${CMAKE_SOURCE_DIR}/libse-thales/legacy/include/driver/i3c
            ${CMAKE_SOURCE_DIR}/libse-thales/legacy/include/linux
            ${CMAKE_SOURCE_DIR}/libse-thales/legacy
            ${CMAKE_BINARY_DIR}
    )
else()
    # Include directories
    target_include_directories(se-thales
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/libse-thales/gp/se-thales>
            $<INSTALL_INTERFACE:include/libse-thales>
        PRIVATE
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/include/core
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/include/driver
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/include/driver/spi
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/include/driver/i3c
            ${CMAKE_SOURCE_DIR}/libse-thales/gp/include/linux
            ${CMAKE_SOURCE_DIR}/libse-thales/gp
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
    ${CMAKE_SOURCE_DIR}/libse-thales/libse-thales.pc.in
    ${CMAKE_SOURCE_DIR}/libse-thales/libse-thales.pc
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
message(STATUS "i3c:                    ${I3C}")
message(STATUS "irq:                    ${IRQ_MODE}")
message(STATUS "gcov coverage:          ${ENABLE_GCOV}")
message(STATUS "se device node:         ${DEVNODE}")
message(STATUS "")

add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CMakeFiles
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/CMakeCache.txt
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/cmake_install.cmake
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/Makefile
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/@ONLY
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/CMakeCache.txt
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/cmake_install.cmake
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/Makefile
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/libse-thalesConfig.cmake
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/libse-thalesConfigVersion.cmake
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/libse-thales.a
    COMMENT "Nettoyage complet des fichiers générés par CMake"
)