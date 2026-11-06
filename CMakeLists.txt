cmake_minimum_required(VERSION 3.25)
project(SketchCraft VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(CRAFT_SANITIZE "Build with address and undefined-behaviour sanitizers" OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Test)
qt_standard_project_setup()

# Some Qt installs (Homebrew) report a plugin folder that does not exist; remember the real one so the
# program can find its platform plugin.
get_filename_component(CRAFT_QT_PLUGINS "${Qt6_DIR}/../../../share/qt/plugins" REALPATH)
if(NOT EXISTS "${CRAFT_QT_PLUGINS}/platforms")
    get_filename_component(CRAFT_QT_PLUGINS "${Qt6_DIR}/../../../plugins" REALPATH)
endif()

set(CORE_SOURCES
    src/skp/Archive.cpp
    src/skp/Classes.cpp
    src/skp/Records.cpp
    src/skp/SkpFile.cpp
    src/skp/Query.cpp
    src/skp/Builder.cpp
    src/geom/Triangulate.cpp
    src/render/Style.cpp
    src/render/SceneBuilder.cpp
    src/render/Sun.cpp
    src/ui/Units.cpp
)

function(craft_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Werror)
        target_compile_definitions(${target} PRIVATE
            _GLIBCXX_ASSERTIONS
            _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE)
    endif()
    if(CRAFT_SANITIZE AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

add_library(sketchcore STATIC ${CORE_SOURCES})
target_include_directories(sketchcore PUBLIC src)
target_link_libraries(sketchcore PUBLIC Qt6::Core Qt6::Gui)
if(EXISTS "${CRAFT_QT_PLUGINS}/platforms")
    target_compile_definitions(sketchcore PUBLIC CRAFT_QT_PLUGIN_DIR="${CRAFT_QT_PLUGINS}")
endif()
craft_warnings(sketchcore)

# The window, 3D view, tools and panels (shared by the app and its window tests).
add_library(sketchui STATIC
    src/ui/MainWindow.cpp
    src/ui/MainWindowUi.cpp
    src/ui/Document.cpp
    src/ui/Icons.cpp
    src/ui/Panels.cpp
    src/render/Viewport.cpp
    src/edit/Tools.cpp
    src/edit/MoreTools.cpp
)
target_link_libraries(sketchui PUBLIC sketchcore Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
craft_warnings(sketchui)

qt_add_executable(sketchcraft src/main.cpp)
set_target_properties(sketchcraft PROPERTIES MACOSX_BUNDLE OFF WIN32_EXECUTABLE ON)
target_link_libraries(sketchcraft PRIVATE sketchui)
craft_warnings(sketchcraft)

qt_add_executable(skptool src/tools/skptool.cpp)
set_target_properties(skptool PROPERTIES MACOSX_BUNDLE OFF)
target_link_libraries(skptool PRIVATE sketchcore)
craft_warnings(skptool)

enable_testing()
qt_add_executable(sketchcraft_tests tests/sketchcraft_tests.cpp)
set_target_properties(sketchcraft_tests PROPERTIES MACOSX_BUNDLE OFF)
target_link_libraries(sketchcraft_tests PRIVATE sketchcore Qt6::Test)
craft_warnings(sketchcraft_tests)
add_test(NAME sketchcraft_tests COMMAND sketchcraft_tests)

qt_add_executable(sketchcraft_ui_tests tests/ui_tests.cpp)
set_target_properties(sketchcraft_ui_tests PROPERTIES MACOSX_BUNDLE OFF)
target_link_libraries(sketchcraft_ui_tests PRIVATE sketchui Qt6::Test)
craft_warnings(sketchcraft_ui_tests)
add_test(NAME sketchcraft_ui_tests COMMAND sketchcraft_ui_tests)
