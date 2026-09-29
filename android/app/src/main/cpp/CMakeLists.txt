cmake_minimum_required(VERSION 3.22.1)
project(tinysynth C)

add_library(tinysynth SHARED tinysynth_jni.c)

target_compile_options(tinysynth PRIVATE
    -O2
    -Wall
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-parameter
)

target_link_libraries(tinysynth log m)
