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

pkg_search_module(FUSE fuse)
if(FUSE_MODULE_NAME)

set(FATXFS_SOURCES ${FATXFS_SOURCES}
                   ${CMAKE_CURRENT_SOURCE_DIR}/fatxfs_fuse.c
                   )

add_executable(fatxfs ${FATXFS_SOURCES})
target_link_libraries(fatxfs fatx ${FUSE_LDFLAGS})
target_include_directories(fatxfs PUBLIC ${FUSE_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/libfatx)
target_compile_options(fatxfs PUBLIC ${FUSE_CFLAGS_OTHER})
install(TARGETS fatxfs DESTINATION bin)

find_program(RONN NAMES ronn)
add_custom_target(fatxfs.1 ${RONN} < ${CMAKE_SOURCE_DIR}/man/fatxfs.1.ronn
                                   > ${CMAKE_BINARY_DIR}/fatxfs.1)

endif()
