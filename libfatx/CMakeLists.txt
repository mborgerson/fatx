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

set(FATX_SOURCES ${FATX_SOURCES}
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx.h
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx_attr.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx_dev.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx_dir.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx_disk.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx_fat.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx_file.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx_internal.h
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx_log.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx_log.h
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx_misc.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx_partition.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/fatx_version.h
                 ${CMAKE_CURRENT_SOURCE_DIR}/ext.h
                 ${CMAKE_CURRENT_SOURCE_DIR}/ext.c
                 PARENT_SCOPE
                 )
