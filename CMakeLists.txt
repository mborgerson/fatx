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

cmake_minimum_required(VERSION 2.6)

project(fatxfs)

find_package(PkgConfig)

set(FATX_SOURCES "")
add_subdirectory(${CMAKE_SOURCE_DIR}/libfatx)
add_library(fatx ${FATX_SOURCES})
target_compile_options(fatx PUBLIC -D_FILE_OFFSET_BITS=64)
if (MSVC)
    target_compile_options(fatx PUBLIC /std:c11)
endif()
set_target_properties(fatx PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (EXISTS ${CMAKE_SOURCE_DIR}/fatxfs)
add_subdirectory(${CMAKE_SOURCE_DIR}/fatxfs)
endif()

if (EXISTS ${CMAKE_SOURCE_DIR}/gfatx)
add_subdirectory(${CMAKE_SOURCE_DIR}/gfatx)
endif()
