cmake_minimum_required(VERSION 3.16)
project(mandelbrot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(mandelbrot src/main.cpp)
target_include_directories(mandelbrot PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(mandelbrot PRIVATE m)

# Enable warnings
target_compile_options(mandelbrot PRIVATE -Wall -Wextra -Wpedantic)
