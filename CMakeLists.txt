cmake_minimum_required(VERSION 3.16)
project(MultiThreadedInstaller VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GUI option
option(BUILD_GUI "Build with GUI support using DuiLib" ON)

# Static linking option for Windows
if(WIN32)
    # When using DuiLib static library, we must use static runtime
    option(STATIC_LINK_RUNTIME "Statically link C/C++ runtime libraries" ON)
    
    if(STATIC_LINK_RUNTIME)
        # Use static runtime library
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        message(STATUS "Using static C/C++ runtime library")
    else()
        # Use dynamic runtime library (default)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        message(STATUS "Using dynamic C/C++ runtime library")
    endif()
endif()

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

# For Windows, prefer static linking if available
if(LIBLZMA_INCLUDE_DIR)
    set(LibLZMA_FOUND TRUE)
    set(LIBLZMA_INCLUDE_DIRS ${LIBLZMA_INCLUDE_DIR})
    message(STATUS "Found liblzma headers: ${LIBLZMA_INCLUDE_DIR}")
    add_definitions(-DLibLZMA_FOUND)
    
    if(WIN32)
        set(LIBLZMA_STATIC_LIB "${THIRD_PARTY_DIR}/xz/lib_static/lzma.lib")
        if(EXISTS ${LIBLZMA_STATIC_LIB})
            set(LIBLZMA_LIBRARIES ${LIBLZMA_STATIC_LIB})
            set(LIBLZMA_STATIC ON)
            message(STATUS "Using liblzma static library: ${LIBLZMA_STATIC_LIB}")
        else()
            # Fallback to dynamic DLL if static lib not found
            set(LIBLZMA_LIBRARIES "")
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
        endif()
    else()
        set(LIBLZMA_LIBRARIES "")  # Use system paths on non-Windows below
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

# DuiLib configuration for GUI
if(BUILD_GUI)
    message(STATUS "Configuring GUI support with DuiLib...")
    
    # Add DuiLib include directory
    set(DUILIB_INCLUDE_DIR ${THIRD_PARTY_DIR}/DuiLib_Ultimate/DuiLib)
    include_directories(${DUILIB_INCLUDE_DIR})
    message(STATUS "DuiLib include directory: ${DUILIB_INCLUDE_DIR}")
    
    # Use static library for single-file installer
    set(USE_DUILIB_STATIC ON CACHE BOOL "Use DuiLib static library")
    
    if(USE_DUILIB_STATIC)
        # Use static library
        set(DUILIB_LIB ${THIRD_PARTY_DIR}/DuiLib_Ultimate/lib_static/DuiLib.lib)
        if(EXISTS ${DUILIB_LIB})
            message(STATUS "Using DuiLib static library: ${DUILIB_LIB}")
            add_definitions(-DUILIB_STATIC)
        else()
            message(FATAL_ERROR "DuiLib static library not found at ${DUILIB_LIB}")
        endif()
    else()
        # Use dynamic library (old behavior)
        if(CMAKE_BUILD_TYPE MATCHES Debug)
            set(DUILIB_LIB ${THIRD_PARTY_DIR}/DuiLib_Ultimate/lib/DuiLib_d.lib)
        else()
            set(DUILIB_LIB ${THIRD_PARTY_DIR}/DuiLib_Ultimate/lib/DuiLib.lib)
        endif()
        
        # Check if DuiLib library exists
        if(NOT EXISTS ${DUILIB_LIB})
            # Try the release version as fallback
            set(DUILIB_LIB ${THIRD_PARTY_DIR}/DuiLib_Ultimate/lib/DuiLib.lib)
            if(NOT EXISTS ${DUILIB_LIB})
                message(WARNING "DuiLib library not found at ${DUILIB_LIB}. GUI support will be disabled.")
                set(BUILD_GUI OFF)
            else()
                message(STATUS "Using DuiLib library (fallback to release): ${DUILIB_LIB}")
            endif()
        else()
            message(STATUS "Using DuiLib library: ${DUILIB_LIB}")
        endif()
    endif()
    
    # Define required Windows libraries for GUI
    set(GUI_WINDOWS_LIBS
        comctl32.lib    # Common Controls
        GdiPlus.lib     # GDI+ for graphics
        Imm32.lib       # Input Method Manager
    )
endif()

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
    
    # Copy DuiLib DLL only if using dynamic library
    if(BUILD_GUI AND NOT USE_DUILIB_STATIC)
        set(DUILIB_DLL_PATH "${THIRD_PARTY_DIR}/DuiLib_Ultimate/bin/DuiLib.dll")
        if(EXISTS ${DUILIB_DLL_PATH})
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DUILIB_DLL_PATH}"
                $<TARGET_FILE_DIR:${target_name}>
                COMMENT "Copying DuiLib DLL to output directory"
            )
        else()
            message(WARNING "DuiLib.dll not found at ${DUILIB_DLL_PATH}")
        endif()
    elseif(BUILD_GUI AND USE_DUILIB_STATIC)
        message(STATUS "Using DuiLib static library - no DLL copy needed")
    endif()
endfunction()

# Source files for packager
set(PACKAGER_SOURCES
    src/packager/folder_scanner.cpp
    src/packager/compression_module.cpp
    src/packager/metadata_generator.cpp
    src/packager/installer_generator.cpp
    src/packager/icon_updater.cpp
    src/packager/version_info_updater.cpp
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
    src/installer/installer_helpers.cpp
    src/installer/registry_utils.cpp
    src/installer/install_state_utils.cpp
    src/installer/uninstall_manager.cpp
    src/installer/main.cpp
)

# GUI source files (only if BUILD_GUI is enabled)
if(BUILD_GUI)
    set(GUI_SOURCES
        # Core GUI management
        src/gui/gui_manager.cpp
        src/gui/page_controller.cpp
        src/gui/installation_worker.cpp
        src/gui/uninstall_worker.cpp
        
        # Page controllers
        src/gui/welcome_page_controller.cpp
        src/gui/progress_page_controller.cpp
        src/gui/completion_page_controller.cpp
        
        # Dialogs and helpers
        src/gui/license_dialog.cpp
        src/gui/message_box_dialog.cpp
        src/gui/gui_helpers.cpp
        
        # Embedded resources manager
        src/installer/embedded_resources.cpp
    )
    list(APPEND INSTALLER_SOURCES ${GUI_SOURCES})
    message(STATUS "Added ${CMAKE_CURRENT_LIST_LENGTH} GUI source files to installer")
endif()

# Common source files
set(COMMON_SOURCES
    src/common/types.cpp
)

# Create packager executable
add_executable(packager ${PACKAGER_SOURCES} ${COMMON_SOURCES})
target_link_libraries(packager ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(packager PRIVATE ${LIBLZMA_INCLUDE_DIRS})
if(LIBLZMA_STATIC)
    target_compile_definitions(packager PRIVATE LZMA_API_STATIC)
endif()
copy_dlls_to_target(packager)

# Create installer executable
if(WIN32 AND BUILD_GUI)
    add_executable(installer WIN32 ${INSTALLER_SOURCES} ${COMMON_SOURCES})
else()
    add_executable(installer ${INSTALLER_SOURCES} ${COMMON_SOURCES})
endif()
target_link_libraries(installer ${LIBLZMA_LIBRARIES} Threads::Threads)
if(WIN32)
    target_link_libraries(installer ole32 shell32)
endif()
target_include_directories(installer PRIVATE ${LIBLZMA_INCLUDE_DIRS})
if(LIBLZMA_STATIC)
    target_compile_definitions(installer PRIVATE LZMA_API_STATIC)
endif()
copy_dlls_to_target(installer)

# Add GUI support to installer if enabled
if(BUILD_GUI)
    # Add GUI preprocessor definition
    target_compile_definitions(installer PRIVATE GUI_ENABLED)
    
    # Configure character set (Unicode) to match DuiLib build.
    if(MSVC)
        target_compile_definitions(installer PRIVATE UNICODE _UNICODE)
        message(STATUS "Configured installer for Unicode character set")
    endif()
    
    # Link DuiLib and required Windows libraries
    target_link_libraries(installer 
        ${DUILIB_LIB}
        ${GUI_WINDOWS_LIBS}
    )
    
    # Embed application manifest for DPI awareness and common controls (Task 11.3)
    if(MSVC)
        # Set the manifest file for the installer executable
        set_target_properties(installer PROPERTIES
            LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_SOURCE_DIR}/src/installer/installer.exe.manifest"
        )
        message(STATUS "Embedding application manifest: ${CMAKE_SOURCE_DIR}/src/installer/installer.exe.manifest")
    endif()
    
    # Copy resources directory to output directory (Task 11.2)
    # This ensures XML layouts and images are available at runtime
    add_custom_command(TARGET installer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:installer>/resources
        COMMENT "Copying GUI resources (XML layouts and images) to output directory"
    )
    
    # Verify critical resource files exist
    set(CRITICAL_RESOURCES
        resources/skins/main.xml
        resources/skins/welcome_page.xml
        resources/skins/progress_page.xml
        resources/skins/completion_page.xml
        resources/skins/license.xml
        resources/license.txt
    )
    
    foreach(RESOURCE ${CRITICAL_RESOURCES})
        if(NOT EXISTS ${CMAKE_SOURCE_DIR}/${RESOURCE})
            message(WARNING "Critical resource file missing: ${RESOURCE}")
        endif()
    endforeach()
    
    message(STATUS "GUI support enabled for installer")
    message(STATUS "  - DuiLib library: ${DUILIB_LIB}")
    message(STATUS "  - Windows GUI libraries: ${GUI_WINDOWS_LIBS}")
    message(STATUS "  - Resources will be copied from: ${CMAKE_SOURCE_DIR}/resources")
endif()
