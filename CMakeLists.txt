cmake_minimum_required(VERSION 3.16)
project(MultiThreadedInstaller VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源码与注释含中文（UTF-8）。强制 MSVC 以 UTF-8 读取源文件，避免多字节字符的
# 尾字节（如 0x5C）吃掉换行导致的诡异解析错误。
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(VERIFY_THIRD_PARTY_HASHES "Validate prebuilt third-party library hashes" ON)
option(ENABLE_ZSTD "Enable ZSTD compression support for packager" ON)
option(ZSTD_FORCE_THIRD_PARTY_BINARY "Force using third_party zstd prebuilt binary on MSVC toolchains" OFF)

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

    if(NOT STATIC_LINK_RUNTIME)
        message(FATAL_ERROR
            "GUI installer builds require STATIC_LINK_RUNTIME=ON because third_party DuiLib static archive is built with /MT.")
    endif()
else()
    message(FATAL_ERROR "The current project phase requires the Windows GUI installer build.")
endif()

# Find required packages
find_package(Threads REQUIRED)

# Set paths for local third-party libraries
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")
set(EXPECTED_LIBLZMA_SHA256 "229209de2a901b987543e1496a02d4874260c3b6f2f2f459bcd6fa2e7bc6f746")
set(EXPECTED_DUILIB_SHA256 "96324b831ff0fd236873f43842945bebfad440a5efaa0d75fd53b9037776164f")
set(EXPECTED_YAML_CPP_SHA256 "1cf6fa319e7529afb38e4ab55e3b0cca7a901f3ee3351b7f525fe682a5b5392e")

set(SPDLOG_INCLUDE_DIR "${THIRD_PARTY_DIR}/spdlog/include")
if(NOT EXISTS "${SPDLOG_INCLUDE_DIR}/spdlog/spdlog.h")
    message(FATAL_ERROR
        "Pinned dependency missing: ${SPDLOG_INCLUDE_DIR}. "
        "This project requires header-only spdlog under third_party/spdlog/include.")
endif()
add_library(spdlog_header_only INTERFACE)
target_include_directories(spdlog_header_only INTERFACE "${SPDLOG_INCLUDE_DIR}")
target_compile_definitions(spdlog_header_only INTERFACE
    SPDLOG_WCHAR_FILENAMES
)

# Try to find zstd (local installation first, then system)
if(ENABLE_ZSTD)
    unset(ZSTD_INCLUDE_DIR CACHE)
    unset(ZSTD_LIBRARIES CACHE)
    set(ZSTD_FOUND FALSE)
    set(ZSTD_LIBRARIES "")
    set(ZSTD_INCLUDE_DIRS "")

    # Header lookup: prefer repository-provided zstd include path.
    find_path(ZSTD_INCLUDE_DIR
        NAMES zstd.h
        PATHS
            "${THIRD_PARTY_DIR}/zstd/include"
            "${THIRD_PARTY_DIR}/zstd/lib"
        NO_DEFAULT_PATH
    )
    if(NOT ZSTD_INCLUDE_DIR)
        find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)
    endif()

    # Library lookup:
    # 1) On MSVC, first try manually built library under third_party/zstd/build-msvc.
    # 2) Then try system package.
    # 3) Finally optional legacy third_party static backup when explicitly requested.
    if(WIN32 AND MSVC)
        set(ZSTD_MSVC_LOCAL_CANDIDATES
            "${THIRD_PARTY_DIR}/zstd/lib_static/zstd_static.lib"
        )

        foreach(ZSTD_CANDIDATE ${ZSTD_MSVC_LOCAL_CANDIDATES})
            if(EXISTS "${ZSTD_CANDIDATE}")
                set(ZSTD_LIBRARIES "${ZSTD_CANDIDATE}")
                message(STATUS "Using MSVC-built zstd static library: ${ZSTD_LIBRARIES}")
                break()
            endif()
        endforeach()

        if(NOT ZSTD_LIBRARIES)
            find_library(ZSTD_LIBRARIES NAMES zstd libzstd_static zstd_static)
            if(ZSTD_LIBRARIES)
                message(STATUS "Using system zstd library: ${ZSTD_LIBRARIES}")
            endif()
        endif()

        if(NOT ZSTD_LIBRARIES AND ZSTD_FORCE_THIRD_PARTY_BINARY)
            set(ZSTD_STATIC_LIB "${THIRD_PARTY_DIR}/zstd/static_backup/libzstd_static.lib")
            if(EXISTS "${ZSTD_STATIC_LIB}")
                set(ZSTD_LIBRARIES "${ZSTD_STATIC_LIB}")
                message(WARNING
                    "Using legacy third_party zstd static backup on MSVC: ${ZSTD_LIBRARIES}. "
                    "If link errors occur, build zstd with MSVC under third_party/zstd/build-msvc.")
            endif()
        endif()
    else()
        if(WIN32)
            set(ZSTD_STATIC_LIB "${THIRD_PARTY_DIR}/zstd/static_backup/libzstd_static.lib")
            if(EXISTS "${ZSTD_STATIC_LIB}")
                set(ZSTD_LIBRARIES "${ZSTD_STATIC_LIB}")
                message(STATUS "Using zstd static library: ${ZSTD_LIBRARIES}")
            endif()
        endif()
        if(NOT ZSTD_LIBRARIES)
            find_library(ZSTD_LIBRARIES NAMES zstd libzstd_static zstd_static)
            if(ZSTD_LIBRARIES)
                message(STATUS "Using system zstd library: ${ZSTD_LIBRARIES}")
            endif()
        endif()
    endif()

    if(ZSTD_INCLUDE_DIR AND ZSTD_LIBRARIES)
        set(ZSTD_FOUND TRUE)
        set(ZSTD_INCLUDE_DIRS "${ZSTD_INCLUDE_DIR}")
        add_definitions(-DZSTD_FOUND)
        message(STATUS "Found zstd headers: ${ZSTD_INCLUDE_DIRS}")
    else()
        message(WARNING
            "zstd not found or incomplete (headers/library). "
            "ZSTD support will be disabled.")
    endif()
endif()

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
            file(SHA256 "${LIBLZMA_STATIC_LIB}" LIBLZMA_SHA256)
            message(STATUS "liblzma static SHA256: ${LIBLZMA_SHA256}")
            if(VERIFY_THIRD_PARTY_HASHES AND NOT LIBLZMA_SHA256 STREQUAL EXPECTED_LIBLZMA_SHA256)
                message(FATAL_ERROR "Unexpected liblzma static hash. Expected ${EXPECTED_LIBLZMA_SHA256}, got ${LIBLZMA_SHA256}")
            endif()
        else()
            message(FATAL_ERROR
                "Pinned dependency missing: ${LIBLZMA_STATIC_LIB}. "
                "This project requires the prebuilt liblzma static archive in third_party/xz/lib_static."
            )
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

# Use pinned yaml-cpp headers and static library from third_party.
find_path(YAML_CPP_INCLUDE_DIR
    NAMES yaml-cpp/yaml.h
    PATHS
        "${THIRD_PARTY_DIR}/yaml-cpp/include"
    NO_DEFAULT_PATH
)

if(NOT YAML_CPP_INCLUDE_DIR)
    message(FATAL_ERROR
        "Pinned dependency missing: ${THIRD_PARTY_DIR}/yaml-cpp/include. "
        "This project requires yaml-cpp headers in third_party/yaml-cpp/include.")
endif()

if(WIN32)
    set(YAML_CPP_STATIC_LIB "${THIRD_PARTY_DIR}/yaml-cpp/lib_static/yaml-cpp.lib")
    if(EXISTS "${YAML_CPP_STATIC_LIB}")
        set(YAML_CPP_LIBRARIES "${YAML_CPP_STATIC_LIB}")
        message(STATUS "Using yaml-cpp static library: ${YAML_CPP_STATIC_LIB}")
        file(SHA256 "${YAML_CPP_STATIC_LIB}" YAML_CPP_SHA256)
        string(TOLOWER "${YAML_CPP_SHA256}" YAML_CPP_SHA256)
        message(STATUS "yaml-cpp static SHA256: ${YAML_CPP_SHA256}")
        if(VERIFY_THIRD_PARTY_HASHES AND NOT YAML_CPP_SHA256 STREQUAL EXPECTED_YAML_CPP_SHA256)
            message(FATAL_ERROR
                "Unexpected yaml-cpp static hash. Expected ${EXPECTED_YAML_CPP_SHA256}, got ${YAML_CPP_SHA256}")
        endif()
    else()
        message(FATAL_ERROR
            "Pinned dependency missing: ${YAML_CPP_STATIC_LIB}. "
            "This project requires the prebuilt yaml-cpp static archive in third_party/yaml-cpp/lib_static.")
    endif()
else()
    message(FATAL_ERROR "The current project phase requires the Windows GUI installer build.")
endif()

# Include directories
include_directories(include)
include_directories(src)
include_directories(${THIRD_PARTY_DIR}/nlohmann)
include_directories(${YAML_CPP_INCLUDE_DIR})

message(STATUS "Configuring GUI support with DuiLib...")

set(DUILIB_INCLUDE_DIR ${THIRD_PARTY_DIR}/DuiLib_Ultimate/DuiLib)
include_directories(${DUILIB_INCLUDE_DIR})
message(STATUS "DuiLib include directory: ${DUILIB_INCLUDE_DIR}")

set(USE_DUILIB_STATIC ON CACHE BOOL "Use DuiLib static library" FORCE)
set(DUILIB_LIB ${THIRD_PARTY_DIR}/DuiLib_Ultimate/lib_static/DuiLib.lib)
if(EXISTS ${DUILIB_LIB})
    message(STATUS "Using DuiLib static library: ${DUILIB_LIB}")
    add_definitions(-DUILIB_STATIC)
    file(SHA256 "${DUILIB_LIB}" DUILIB_SHA256)
    message(STATUS "DuiLib static SHA256: ${DUILIB_SHA256}")
    if(VERIFY_THIRD_PARTY_HASHES AND NOT DUILIB_SHA256 STREQUAL EXPECTED_DUILIB_SHA256)
        message(FATAL_ERROR "Unexpected DuiLib static hash. Expected ${EXPECTED_DUILIB_SHA256}, got ${DUILIB_SHA256}")
    endif()
else()
    message(FATAL_ERROR "DuiLib static library not found at ${DUILIB_LIB}")
endif()

set(GUI_WINDOWS_LIBS
    comctl32.lib
    GdiPlus.lib
    Imm32.lib
)

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
    
    if(USE_DUILIB_STATIC)
        message(STATUS "Using DuiLib static library - no DLL copy needed")
    endif()
endfunction()

# Handle known CRT mismatch noise from prebuilt liblzma static archive.
# The project runtime stays /MT to match DuiLib static library.
function(apply_msvc_liblzma_link_fixes target_name)
    if(MSVC AND WIN32 AND STATIC_LINK_RUNTIME AND LIBLZMA_STATIC)
        target_link_options(${target_name} PRIVATE
            /NODEFAULTLIB:MSVCRT
            /IGNORE:4098
            /IGNORE:4217
        )
    endif()
endfunction()

# Source files for packager
set(PACKAGER_SOURCES
    src/packager/folder_scanner.cpp
    src/packager/compression_module.cpp
    src/packager/folder_payload_compressor.cpp
    src/packager/package_manifest_builder.cpp
    src/packager/installer_generator.cpp
    src/packager/icon_updater.cpp
    src/packager/version_info_updater.cpp
    src/packager/template_loader.cpp
    src/packager/pe_resource_embedder.cpp
    src/packager/resource_zip_builder.cpp
    src/packager/config_value_reader.cpp
    src/packager/configuration_loader.cpp
    src/packager/configuration_validator.cpp
    src/packager/configuration_manager.cpp
    src/packager/main.cpp
    src/installer/app/console_interface.cpp
)

# Source files for installer
set(INSTALLER_SOURCES
    src/installer/payload/metadata_parser.cpp
    src/installer/payload/package_manifest_validator.cpp
    src/installer/pipeline/installer_task_manager.cpp
    src/installer/pipeline/thread_pool_manager.cpp
    src/installer/payload/decompression_engine.cpp
    src/installer/payload/folder_payload_reader.cpp
    src/installer/payload/folder_install_executor.cpp
    src/installer/payload/tar_stream_extractor.cpp
    src/installer/platform/file_system_operator.cpp
    src/installer/hooks/component_launcher.cpp
    src/installer/hooks/hook_runner.cpp
    src/installer/migration/migration.cpp
    src/installer/app/console_interface.cpp
    src/installer/platform/path_resolver.cpp
    src/installer/platform/installer_helpers.cpp
    src/installer/platform/file_io_utils.cpp
    src/installer/platform/shortcut_startup_utils.cpp
    src/installer/state/registry_utils.cpp
    src/installer/state/install_state_utils.cpp
    src/installer/state/install_state_store.cpp
    src/installer/pipeline/install_progress_reporter.cpp
    src/installer/pipeline/install_plan_builder.cpp
    src/installer/pipeline/install_precheck.cpp
    src/installer/pipeline/install_cleanup_executor.cpp
    src/installer/pipeline/installer_concurrency_policy.cpp
    src/installer/uninstall/cleanup_delete_executor.cpp
    src/installer/pipeline/install_execution.cpp
    src/installer/pipeline/install_finalize.cpp
    src/installer/pipeline/install_service.cpp
    src/installer/state/install_manifest_store.cpp
    src/installer/state/installed_instance_resolver.cpp
    src/installer/uninstall/self_delete_scheduler.cpp
    src/installer/uninstall/upgrade_cleanup.cpp
    src/installer/uninstall/uninstall_manager.cpp
    src/installer/platform/gui_resource_loader.cpp
    src/installer/app/launch_support.cpp
    src/installer/app/single_instance.cpp
    src/installer/app/installer_main.cpp
    src/common/installer_parallel_install.cpp
)

set(UNINSTALLER_SOURCES
    src/installer/payload/metadata_parser.cpp
    src/installer/payload/package_manifest_validator.cpp
    src/installer/pipeline/installer_task_manager.cpp
    src/installer/pipeline/thread_pool_manager.cpp
    src/installer/payload/decompression_engine.cpp
    src/installer/payload/folder_payload_reader.cpp
    src/installer/payload/folder_install_executor.cpp
    src/installer/payload/tar_stream_extractor.cpp
    src/installer/platform/file_system_operator.cpp
    src/installer/hooks/component_launcher.cpp
    src/installer/hooks/hook_runner.cpp
    src/installer/migration/migration.cpp
    src/installer/app/console_interface.cpp
    src/installer/platform/path_resolver.cpp
    src/installer/platform/installer_helpers.cpp
    src/installer/platform/file_io_utils.cpp
    src/installer/platform/shortcut_startup_utils.cpp
    src/installer/state/registry_utils.cpp
    src/installer/state/install_state_utils.cpp
    src/installer/state/install_state_store.cpp
    src/installer/pipeline/install_progress_reporter.cpp
    src/installer/pipeline/install_plan_builder.cpp
    src/installer/pipeline/install_precheck.cpp
    src/installer/pipeline/install_cleanup_executor.cpp
    src/installer/pipeline/installer_concurrency_policy.cpp
    src/installer/uninstall/cleanup_delete_executor.cpp
    src/installer/pipeline/install_execution.cpp
    src/installer/pipeline/install_finalize.cpp
    src/installer/pipeline/install_service.cpp
    src/installer/state/install_manifest_store.cpp
    src/installer/state/installed_instance_resolver.cpp
    src/installer/uninstall/self_delete_scheduler.cpp
    src/installer/uninstall/upgrade_cleanup.cpp
    src/installer/uninstall/uninstall_manager.cpp
    src/installer/platform/gui_resource_loader.cpp
    src/installer/app/launch_support.cpp
    src/installer/app/single_instance.cpp
    src/installer/app/uninstaller_main.cpp
    src/common/installer_parallel_install.cpp
)

set(GUI_SOURCES
    src/gui/core/gui_manager.cpp
    src/gui/core/gui_runtime_utils.cpp
    src/gui/presenters/gui_text_presenter.cpp
    src/gui/presenters/gui_status_presenter.cpp
    src/gui/presenters/progress_path_formatter.cpp
    src/gui/presenters/gui_diagnostics.cpp
    src/gui/core/gui_event_router.cpp
    src/gui/core/gui_startup_initializer.cpp
    src/gui/core/gui_interaction_helpers.cpp
    src/gui/install/gui_install_flow_utils.cpp
    src/gui/install/gui_install_actions.cpp
    src/gui/pages/page_controller.cpp
    src/gui/workers/installation_worker.cpp
    src/gui/workers/uninstall_worker.cpp
    src/gui/pages/welcome_page_controller.cpp
    src/gui/pages/progress_page_controller.cpp
    src/gui/pages/completion_page_controller.cpp
    src/gui/dialogs/license_dialog.cpp
    src/gui/dialogs/license_text_loader.cpp
    src/gui/dialogs/message_box_dialog.cpp
    src/gui/core/gui_helpers.cpp
    src/installer/platform/embedded_resources.cpp
)
list(APPEND INSTALLER_SOURCES ${GUI_SOURCES})
list(APPEND UNINSTALLER_SOURCES ${GUI_SOURCES})
message(STATUS "Added GUI source files to installer")

# Common source files
set(COMMON_SOURCES
    src/common/types.cpp
    src/common/installer_logger.cpp
    src/common/lzma_loader.cpp
    src/common/utf8_utils.cpp
    src/common/version_utils.cpp
    src/common/package_manifest.cpp
    src/common/package_manifest_codec.cpp
)

# Create packager executable
add_executable(packager ${PACKAGER_SOURCES} ${COMMON_SOURCES})
target_link_libraries(packager spdlog_header_only ${LIBLZMA_LIBRARIES} ${ZSTD_LIBRARIES} Threads::Threads ${YAML_CPP_LIBRARIES})
if(WIN32)
    target_link_libraries(packager dbghelp)
endif()
target_include_directories(packager PRIVATE ${LIBLZMA_INCLUDE_DIRS} ${ZSTD_INCLUDE_DIRS})
target_compile_definitions(packager PRIVATE YAML_CPP_STATIC_DEFINE)
if(LIBLZMA_STATIC)
    target_compile_definitions(packager PRIVATE LZMA_API_STATIC)
endif()
apply_msvc_liblzma_link_fixes(packager)
copy_dlls_to_target(packager)

# Create installer executable
add_executable(installer WIN32 ${INSTALLER_SOURCES} ${COMMON_SOURCES})
target_link_libraries(installer spdlog_header_only ${LIBLZMA_LIBRARIES} ${ZSTD_LIBRARIES} Threads::Threads)
if(WIN32)
    target_link_libraries(installer ole32 shell32 bcrypt winhttp dbghelp rstrtmgr)
endif()
target_include_directories(installer PRIVATE ${LIBLZMA_INCLUDE_DIRS} ${ZSTD_INCLUDE_DIRS})
if(LIBLZMA_STATIC)
    target_compile_definitions(installer PRIVATE LZMA_API_STATIC)
endif()
apply_msvc_liblzma_link_fixes(installer)
copy_dlls_to_target(installer)

add_executable(uninstaller WIN32 ${UNINSTALLER_SOURCES} ${COMMON_SOURCES})
target_link_libraries(uninstaller spdlog_header_only ${LIBLZMA_LIBRARIES} ${ZSTD_LIBRARIES} Threads::Threads)
if(WIN32)
    target_link_libraries(uninstaller ole32 shell32 bcrypt winhttp dbghelp rstrtmgr)
endif()
target_include_directories(uninstaller PRIVATE ${LIBLZMA_INCLUDE_DIRS} ${ZSTD_INCLUDE_DIRS})
if(LIBLZMA_STATIC)
    target_compile_definitions(uninstaller PRIVATE LZMA_API_STATIC)
endif()
apply_msvc_liblzma_link_fixes(uninstaller)
copy_dlls_to_target(uninstaller)

if(MSVC)
    target_compile_definitions(installer PRIVATE UNICODE _UNICODE)
    target_compile_definitions(uninstaller PRIVATE UNICODE _UNICODE)
    message(STATUS "Configured installer for Unicode character set")
endif()

target_link_libraries(installer 
    ${DUILIB_LIB}
    ${GUI_WINDOWS_LIBS}
)
target_link_libraries(uninstaller
    ${DUILIB_LIB}
    ${GUI_WINDOWS_LIBS}
)

if(MSVC)
    set_target_properties(installer PROPERTIES
        LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_SOURCE_DIR}/src/installer/installer.exe.manifest"
    )
    set_target_properties(uninstaller PROPERTIES
        LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_SOURCE_DIR}/src/installer/installer.exe.manifest"
    )
    message(STATUS "Embedding application manifest: ${CMAKE_SOURCE_DIR}/src/installer/installer.exe.manifest")
endif()

add_custom_command(TARGET installer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/resources
    $<TARGET_FILE_DIR:installer>/resources
    COMMENT "Copying GUI resources (XML layouts and images) to output directory"
)

add_custom_command(TARGET uninstaller POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/resources
    $<TARGET_FILE_DIR:uninstaller>/resources
    COMMENT "Copying GUI resources (XML layouts and images) to output directory"
)

set(CRITICAL_RESOURCES
    resources/skins/main.xml
    resources/skins/welcome_page.xml
    resources/skins/progress_page.xml
    resources/skins/completion_page.xml
    resources/skins/license_page.xml
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

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()

