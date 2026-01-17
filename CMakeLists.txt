cmake_minimum_required(VERSION 3.16)
project(MultiThreadedInstaller VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

# Set paths for local third-party libraries
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

# Try to find liblzma (local installation first, then system)
find_path(LIBLZMA_INCLUDE_DIR 
    NAMES lzma.h
    PATHS 
        "${THIRD_PARTY_DIR}/xz/include"
    NO_DEFAULT_PATH
)

# For Windows, we'll use dynamic loading instead of linking
if(LIBLZMA_INCLUDE_DIR)
    set(LibLZMA_FOUND TRUE)
    set(LIBLZMA_LIBRARIES "")  # No libraries to link, we'll use dynamic loading
    set(LIBLZMA_INCLUDE_DIRS ${LIBLZMA_INCLUDE_DIR})
    message(STATUS "Found liblzma headers: ${LIBLZMA_INCLUDE_DIR}")
    add_definitions(-DLibLZMA_FOUND)
    
    # Copy DLL to output directory for runtime
    find_file(LIBLZMA_DLL
        NAMES liblzma.dll
        PATHS 
            "${THIRD_PARTY_DIR}/xz/bin_x86-64"
            "${THIRD_PARTY_DIR}/xz/bin_i686-sse2"
        NO_DEFAULT_PATH
    )
    
    if(LIBLZMA_DLL)
        message(STATUS "Found liblzma DLL: ${LIBLZMA_DLL}")
        set(LIBLZMA_DLL_PATH ${LIBLZMA_DLL})
    endif()
else()
    # If not found locally, try system paths
    find_package(LibLZMA QUIET)
    if(LibLZMA_FOUND)
        message(STATUS "Found liblzma (system): ${LIBLZMA_LIBRARIES}")
        add_definitions(-DLibLZMA_FOUND)
    else()
        message(WARNING "liblzma not found - using stub implementation")
        set(LIBLZMA_LIBRARIES "")
        set(LIBLZMA_INCLUDE_DIRS "")
    endif()
endif()

# Include directories
include_directories(include)
include_directories(src)
include_directories(${THIRD_PARTY_DIR}/nlohmann)

# Function to copy DLLs to target directory
function(copy_dlls_to_target target_name)
    if(LIBLZMA_DLL_PATH)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBLZMA_DLL_PATH}"
            $<TARGET_FILE_DIR:${target_name}>
            COMMENT "Copying LZMA DLL to output directory"
        )
    endif()
endfunction()

# Source files for packager
set(PACKAGER_SOURCES
    src/packager/folder_scanner.cpp
    src/packager/compression_module.cpp
    src/packager/metadata_generator.cpp
    src/packager/installer_generator.cpp
    src/packager/configuration_loader.cpp
    src/packager/configuration_validator.cpp
    src/packager/configuration_manager.cpp
    src/packager/main.cpp
    src/installer/console_interface.cpp
)

# Source files for installer
set(INSTALLER_SOURCES
    src/installer/metadata_parser.cpp
    src/installer/thread_pool_manager.cpp
    src/installer/decompression_engine.cpp
    src/installer/tar_stream_extractor.cpp
    src/installer/file_system_operator.cpp
    src/installer/console_interface.cpp
    src/installer/path_resolver.cpp
    src/installer/main.cpp
)

# Common source files
set(COMMON_SOURCES
    src/common/types.cpp
    src/common/lzma_loader.cpp
)

# Create packager executable
add_executable(packager ${PACKAGER_SOURCES} ${COMMON_SOURCES})
target_link_libraries(packager ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(packager PRIVATE ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(packager)

# Create installer executable
add_executable(installer ${INSTALLER_SOURCES} ${COMMON_SOURCES})
target_link_libraries(installer ${LIBLZMA_LIBRARIES} Threads::Threads)
if(WIN32)
    target_link_libraries(installer ole32 shell32)
endif()
target_include_directories(installer PRIVATE ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(installer)

