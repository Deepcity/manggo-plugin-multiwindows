cmake_minimum_required(VERSION 3.21)
project(manggo_multiwindows_native LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(manggo-multiwindows MODULE src/multiwindows.cpp)
target_compile_features(manggo-multiwindows PRIVATE cxx_std_17)
target_compile_definitions(manggo-multiwindows PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(manggo-multiwindows PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets user32)
set_target_properties(manggo-multiwindows PROPERTIES PREFIX "")

if(MSVC)
  target_compile_options(manggo-multiwindows PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
