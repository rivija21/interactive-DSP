cmake_minimum_required(VERSION 3.20)
project(FilterLab LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

file(GLOB_RECURSE FILTERLAB_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")

# The icon is drawn by tools/make_icon.cpp (the port of scripts/make_icon.swift).
set(FILTERLAB_ICON "${CMAKE_SOURCE_DIR}/res/FilterLab.ico")
add_executable(make_icon tools/make_icon.cpp)
target_compile_definitions(make_icon PRIVATE UNICODE _UNICODE)
target_link_libraries(make_icon PRIVATE d2d1 windowscodecs ole32 uuid)
if(MINGW)
  target_link_options(make_icon PRIVATE -municode -static)
endif()
add_custom_command(OUTPUT "${FILTERLAB_ICON}"
  COMMAND make_icon "${FILTERLAB_ICON}"
  DEPENDS make_icon
  COMMENT "Drawing icon")
add_custom_target(icon DEPENDS "${FILTERLAB_ICON}")

add_executable(FilterLab WIN32 ${FILTERLAB_SOURCES} res/FilterLab.rc)
add_dependencies(FilterLab icon)
set_source_files_properties(res/FilterLab.rc PROPERTIES OBJECT_DEPENDS "${FILTERLAB_ICON};${CMAKE_SOURCE_DIR}/res/FilterLab.manifest")
target_include_directories(FilterLab PRIVATE src)
target_compile_definitions(FilterLab PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(FilterLab PRIVATE
  d2d1 d3d11 dxgi dwrite windowscodecs ole32 oleaut32 uuid shell32 comctl32 comdlg32 dwmapi winmm avrt
  mfplat mfreadwrite mfuuid user32 gdi32)

if(MSVC)
  target_compile_options(FilterLab PRIVATE /utf-8 /W3 /permissive- /EHsc)
  target_compile_definitions(FilterLab PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(make_icon PRIVATE /utf-8)
  # The manifest comes from the resource file.
  target_link_options(FilterLab PRIVATE /MANIFEST:NO)
  set_property(TARGET FilterLab make_icon PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  target_compile_options(FilterLab PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -Wno-unused-parameter
    -Wno-dangling-reference -Wno-missing-field-initializers -Wno-cast-function-type)
  # One self-contained .exe: no libstdc++ / winpthread DLLs needed next to it.
  target_link_options(FilterLab PRIVATE -municode -static -static-libgcc -static-libstdc++ -s)
endif()

# Test builds can save exports without a dialog (see platform/Dialogs.cpp). Off for releases.
option(FILTERLAB_TEST_HOOKS "Build with automated-test hooks" OFF)
if(FILTERLAB_TEST_HOOKS)
  target_compile_definitions(FilterLab PRIVATE FILTERLAB_TEST_HOOKS)
endif()

set_target_properties(FilterLab PROPERTIES OUTPUT_NAME "FilterLab" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
