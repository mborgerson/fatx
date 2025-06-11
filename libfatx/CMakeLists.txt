# FATX Filesystem Library
#
# Copyright (C) 2015  Matt Borgerson
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.5)

project(libfatx VERSION 1.0)

add_library(fatx STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/fatx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fatx_attr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fatx_dev.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fatx_dir.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fatx_disk.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fatx_fat.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fatx_file.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fatx_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fatx_misc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fatx_partition.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ext.c
    )

set_target_properties(fatx PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(fatx PUBLIC -D_FILE_OFFSET_BITS=64)
if (MSVC)
    target_compile_options(fatx PUBLIC /std:c11)
endif()

install(TARGETS fatx
        EXPORT fatxTargets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
        )
install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/fatx.h
        ${CMAKE_CURRENT_SOURCE_DIR}/fatx_log.h
        ${CMAKE_CURRENT_SOURCE_DIR}/fatx_version.h
        DESTINATION include)

# Export the target
install(EXPORT fatxTargets
    FILE fatxTargets.cmake
    NAMESPACE libfatx::
    DESTINATION lib/cmake/libfatx
)

# Generate the config files
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libfatxConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/libfatxConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libfatxConfig.cmake"
    INSTALL_DESTINATION lib/cmake/libfatx
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libfatxConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libfatxConfigVersion.cmake"
    DESTINATION lib/cmake/libfatx
)