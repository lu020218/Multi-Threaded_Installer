cmake_minimum_required(VERSION 3.16)
project(FileLockSimulator LANGUAGES CXX)

add_executable(FileLockSimulator FileLockSimulator.cpp)

target_compile_features(FileLockSimulator PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(FileLockSimulator PRIVATE /W4 /EHsc)
endif()