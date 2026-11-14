cmake_minimum_required(VERSION 3.21)
project(manggo_multiwindows_loader LANGUAGES CXX)

add_executable(manggo-multiwindows-loader src/main.cpp)
target_compile_features(manggo-multiwindows-loader PRIVATE cxx_std_17)
target_compile_definitions(manggo-multiwindows-loader PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(manggo-multiwindows-loader PRIVATE psapi)

if(MSVC)
  target_compile_options(manggo-multiwindows-loader PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
