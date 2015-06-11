project(qmlcompmgr)
cmake_minimum_required(VERSION 3.2)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall)

find_package(Qt5 5.4.0 REQUIRED Core Gui Quick Qml X11Extras)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED X11)

add_library(libqmlcompmgr STATIC
            compositor.h
            compositor.cpp
            windowpixmap.h
            windowpixmap.cpp
            clientwindow.h
            clientwindow.cpp
            glxtexturefrompixmap.h
            glxtexturefrompixmap.cpp
            windowpixmapitem.h
            windowpixmapitem.cpp)
set_property(TARGET libqmlcompmgr PROPERTY OUTPUT_NAME qmlcompmgr)
target_include_directories(libqmlcompmgr INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(libqmlcompmgr PRIVATE
                           "${OPENGL_INCLUDE_DIR}"
                           "${X11_X11_INCLUDE_PATH}")
target_link_libraries(libqmlcompmgr
                      Qt5::Core
                      Qt5::Gui
                      Qt5::Quick
                      Qt5::Qml
                      Qt5::X11Extras
                      xcb
                      xcb-util
                      xcb-damage
                      xcb-composite
                      xcb-xfixes
                      xcb-shape
                      xcb-render
                      xcb-render-util
                      "${OPENGL_gl_LIBRARY}"
                      "${X11_X11_LIB}")

add_executable(qmlcompmgr main.cpp qmlcompmgr.qrc)
target_link_libraries(qmlcompmgr libqmlcompmgr)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()
