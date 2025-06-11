cmake_minimum_required(VERSION 3.5)

project(gfatx LANGUAGES CXX)

find_package(libfatx REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Widgets)
if(Qt6_FOUND)
    find_package(Qt6 COMPONENTS Widgets REQUIRED)
    add_executable(gfatx
      main.cpp
      mainwindow.cpp
      mainwindow.h
      mainwindow.ui
      fatxfilesystemmodel.cpp
      fatxfilesystemmodel.h
    )

    target_link_libraries(gfatx PRIVATE Qt6::Widgets libfatx::fatx)
    install(TARGETS gfatx DESTINATION bin)
endif()
