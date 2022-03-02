# cmake_minimum_required(VERSION 3.5)

# project(gfatx LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 COMPONENTS Widgets)
if(Qt5_FOUND)
    find_package(Qt5 COMPONENTS Widgets REQUIRED)
    add_executable(gfatx
      main.cpp
      mainwindow.cpp
      mainwindow.h
      mainwindow.ui
      fatxfilesystemmodel.cpp
      fatxfilesystemmodel.h
    )

    target_link_libraries(gfatx PRIVATE Qt5::Widgets fatx)
    target_include_directories(gfatx PUBLIC ${CMAKE_SOURCE_DIR}/libfatx)
endif()
