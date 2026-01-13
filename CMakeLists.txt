cmake_minimum_required(VERSION 3.16)
project(MultiThreadedInstaller VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

# Set paths for local third-party libraries
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

# Try to find zstd (local installation first, then system)
find_path(ZSTD_INCLUDE_DIR 
    NAMES zstd.h
    PATHS 
        "${THIRD_PARTY_DIR}/zstd/include"
        "${THIRD_PARTY_DIR}/zstd/zstd-v1.5.7-win64/include"
    NO_DEFAULT_PATH
)

find_library(ZSTD_LIBRARY 
    NAMES libzstd.dll.a zstd libzstd libzstd_static
    PATHS 
        "${THIRD_PARTY_DIR}/zstd/dll"
        "${THIRD_PARTY_DIR}/zstd/zstd-v1.5.7-win64/dll"
        "${THIRD_PARTY_DIR}/zstd/static"
        "${THIRD_PARTY_DIR}/zstd/zstd-v1.5.7-win64/static"
    NO_DEFAULT_PATH
)

# If not found locally, try system paths
if(NOT ZSTD_INCLUDE_DIR OR NOT ZSTD_LIBRARY)
    find_path(ZSTD_INCLUDE_DIR zstd.h)
    find_library(ZSTD_LIBRARY zstd)
endif()

if(ZSTD_INCLUDE_DIR AND ZSTD_LIBRARY)
    set(ZSTD_FOUND TRUE)
    set(ZSTD_LIBRARIES ${ZSTD_LIBRARY})
    set(ZSTD_INCLUDE_DIRS ${ZSTD_INCLUDE_DIR})
    message(STATUS "Found zstd: ${ZSTD_LIBRARY}")
    add_definitions(-DZSTD_FOUND)
    
    # Find and copy ZSTD DLL for runtime
    find_file(ZSTD_DLL
        NAMES libzstd.dll
        PATHS 
            "${THIRD_PARTY_DIR}/zstd/dll"
            "${THIRD_PARTY_DIR}/zstd/zstd-v1.5.7-win64/dll"
        NO_DEFAULT_PATH
    )
    
    if(ZSTD_DLL)
        message(STATUS "Found ZSTD DLL: ${ZSTD_DLL}")
        set(ZSTD_DLL_PATH ${ZSTD_DLL})
    endif()
else()
    set(ZSTD_FOUND FALSE)
    message(WARNING "zstd library not found - using stub implementation")
    set(ZSTD_LIBRARIES "")
    set(ZSTD_INCLUDE_DIRS "")
endif()

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
    if(ZSTD_DLL_PATH)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ZSTD_DLL_PATH}"
            $<TARGET_FILE_DIR:${target_name}>
            COMMENT "Copying ZSTD DLL to output directory"
        )
    endif()
    
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
target_link_libraries(packager ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(packager PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(packager)

# Create installer executable
add_executable(installer ${INSTALLER_SOURCES} ${COMMON_SOURCES})
target_link_libraries(installer ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(installer PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(installer)

# Testing
enable_testing()

# Find RapidCheck for property-based testing
find_path(RAPIDCHECK_INCLUDE_DIR rapidcheck.h PATHS /usr/include /usr/local/include)
find_library(RAPIDCHECK_LIBRARY rapidcheck PATHS /usr/lib /usr/local/lib)

if(RAPIDCHECK_INCLUDE_DIR AND RAPIDCHECK_LIBRARY)
    set(RAPIDCHECK_FOUND TRUE)
    message(STATUS "RapidCheck found")
else()
    set(RAPIDCHECK_FOUND FALSE)
    message(WARNING "RapidCheck not found - property-based tests will be disabled")
endif()

# Create individual test executables to avoid main function conflicts
add_executable(test_folder_scanner tests/test_folder_scanner.cpp ${COMMON_SOURCES}
    src/packager/folder_scanner.cpp
)
target_link_libraries(test_folder_scanner ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_folder_scanner PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_folder_scanner)

add_executable(test_compression_module tests/test_compression_module.cpp ${COMMON_SOURCES}
    src/packager/compression_module.cpp
)
target_link_libraries(test_compression_module ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_compression_module PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_compression_module)

add_executable(test_metadata_generator tests/test_metadata_generator.cpp ${COMMON_SOURCES}
    src/packager/metadata_generator.cpp
    src/packager/folder_scanner.cpp
    src/packager/compression_module.cpp
)
target_link_libraries(test_metadata_generator ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_metadata_generator PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_metadata_generator)

add_executable(test_decompression_engine tests/test_decompression_engine.cpp ${COMMON_SOURCES}
    src/installer/decompression_engine.cpp
    src/installer/thread_pool_manager.cpp
)
target_link_libraries(test_decompression_engine ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_decompression_engine PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_decompression_engine)

add_executable(test_file_system_operator tests/test_file_system_operator.cpp ${COMMON_SOURCES}
    src/installer/file_system_operator.cpp
)
target_link_libraries(test_file_system_operator ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_file_system_operator PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_file_system_operator)

# Create separate test executable for metadata management
add_executable(test_metadata_management tests/test_metadata_management.cpp ${COMMON_SOURCES}
    src/packager/folder_scanner.cpp
    src/packager/compression_module.cpp
    src/packager/metadata_generator.cpp
    src/installer/metadata_parser.cpp
)
target_link_libraries(test_metadata_management ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_metadata_management PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_metadata_management)

# Create test executable for installer generator
add_executable(test_installer_generator tests/test_installer_generator.cpp ${COMMON_SOURCES}
    src/packager/installer_generator.cpp
    src/packager/metadata_generator.cpp
    src/installer/metadata_parser.cpp
    src/packager/folder_scanner.cpp
    src/packager/compression_module.cpp
)
target_link_libraries(test_installer_generator ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_installer_generator PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_installer_generator)

# Create test executable for thread pool manager
add_executable(test_thread_pool_manager tests/test_thread_pool_manager.cpp ${COMMON_SOURCES}
    src/installer/thread_pool_manager.cpp
)
target_link_libraries(test_thread_pool_manager ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_thread_pool_manager PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_thread_pool_manager)

# Create property-based test executable for thread pool manager
add_executable(test_thread_pool_pbt tests/pbt/test_thread_pool_parallel_processing.cpp ${COMMON_SOURCES}
    src/installer/thread_pool_manager.cpp
)
target_link_libraries(test_thread_pool_pbt ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_thread_pool_pbt PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_thread_pool_pbt)

# Create property-based test executable for decompression error recovery
add_executable(test_decompression_error_recovery tests/pbt/test_decompression_error_recovery.cpp ${COMMON_SOURCES}
    src/installer/decompression_engine.cpp
    src/installer/thread_pool_manager.cpp
    src/installer/file_system_operator.cpp
    src/packager/compression_module.cpp
    src/packager/folder_scanner.cpp
)
target_link_libraries(test_decompression_error_recovery ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_decompression_error_recovery PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_decompression_error_recovery)

# Create property-based test executable for file system operator
add_executable(test_file_system_operator_pbt tests/pbt/test_file_system_operator_properties.cpp ${COMMON_SOURCES}
    src/installer/file_system_operator.cpp
)
target_link_libraries(test_file_system_operator_pbt ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_file_system_operator_pbt PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_file_system_operator_pbt)

# Create property-based test executable for status report accuracy
add_executable(test_status_report_accuracy tests/pbt/test_status_report_accuracy.cpp ${COMMON_SOURCES}
    src/installer/metadata_parser.cpp
    src/installer/thread_pool_manager.cpp
    src/installer/decompression_engine.cpp
    src/installer/file_system_operator.cpp
    src/installer/console_interface.cpp
    src/packager/folder_scanner.cpp
    src/packager/compression_module.cpp
    src/packager/metadata_generator.cpp
    src/packager/installer_generator.cpp
)
target_link_libraries(test_status_report_accuracy ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_status_report_accuracy PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_status_report_accuracy)

# Property-based test sources
if(RAPIDCHECK_FOUND)
    set(PBT_SOURCES
        tests/pbt/test_compression_roundtrip.cpp
        tests/pbt/test_folder_scanner_integrity.cpp
        tests/pbt/test_compression_properties.cpp
        tests/pbt/test_metadata_mapping_consistency.cpp
        tests/pbt/test_installer_generator_embedding.cpp
        tests/pbt/test_thread_pool_parallel_processing.cpp
    )
endif()

# Add property-based tests if RapidCheck is available
if(RAPIDCHECK_FOUND)
    add_executable(pbt_tests ${PBT_SOURCES} ${COMMON_SOURCES}
        src/packager/folder_scanner.cpp
        src/packager/compression_module.cpp
        src/packager/metadata_generator.cpp
        src/packager/installer_generator.cpp
        src/installer/metadata_parser.cpp
        src/installer/decompression_engine.cpp
        src/installer/thread_pool_manager.cpp
    )
    target_link_libraries(pbt_tests ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} ${RAPIDCHECK_LIBRARY} Threads::Threads)
    target_include_directories(pbt_tests PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS} ${RAPIDCHECK_INCLUDE_DIR})
    copy_dlls_to_target(pbt_tests)
    
    add_test(NAME PropertyBasedTests COMMAND pbt_tests)
endif()

# Create test executable for console interface
add_executable(test_console_interface tests/test_console_interface.cpp ${COMMON_SOURCES}
    src/installer/console_interface.cpp
)
target_link_libraries(test_console_interface ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_console_interface PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_console_interface)

# Create test executable for configuration structures
add_executable(test_configuration_structures tests/test_configuration_structures.cpp ${COMMON_SOURCES})
target_link_libraries(test_configuration_structures ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_configuration_structures PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_configuration_structures)

# Create test executable for configuration loader
add_executable(test_configuration_loader tests/test_configuration_loader.cpp ${COMMON_SOURCES}
    src/packager/configuration_loader.cpp
)
target_link_libraries(test_configuration_loader ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_configuration_loader PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_configuration_loader)

# Create test executable for configuration validator
add_executable(test_configuration_validator tests/test_configuration_validator.cpp ${COMMON_SOURCES}
    src/packager/configuration_validator.cpp
)
target_link_libraries(test_configuration_validator ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_configuration_validator PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_configuration_validator)

# Create property-based test executable for configuration loader
add_executable(test_configuration_loader_pbt tests/pbt/test_configuration_loader_properties.cpp ${COMMON_SOURCES}
    src/packager/configuration_loader.cpp
)
target_link_libraries(test_configuration_loader_pbt ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_configuration_loader_pbt PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_configuration_loader_pbt)

# Create property-based test executable for configuration validator
add_executable(test_configuration_validator_pbt tests/pbt/test_configuration_validator_properties.cpp ${COMMON_SOURCES}
    src/packager/configuration_validator.cpp
)
target_link_libraries(test_configuration_validator_pbt ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_configuration_validator_pbt PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_configuration_validator_pbt)

# Create property-based test executable for metadata generator
add_executable(test_metadata_generator_pbt tests/pbt/test_metadata_generator_properties.cpp ${COMMON_SOURCES}
    src/packager/metadata_generator.cpp
    src/packager/folder_scanner.cpp
    src/packager/compression_module.cpp
)
target_link_libraries(test_metadata_generator_pbt ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_metadata_generator_pbt PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_metadata_generator_pbt)

# Create property-based test executable for packager integration
add_executable(test_packager_integration_pbt tests/pbt/test_packager_integration_properties.cpp ${COMMON_SOURCES})
target_link_libraries(test_packager_integration_pbt ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_packager_integration_pbt PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_packager_integration_pbt)

# Create property-based test executable for path resolver
add_executable(test_path_resolver_pbt tests/pbt/test_path_resolver_properties.cpp ${COMMON_SOURCES}
    src/installer/path_resolver.cpp
)
target_link_libraries(test_path_resolver_pbt ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_path_resolver_pbt PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_path_resolver_pbt)

# Create test executable for configuration manager
add_executable(test_configuration_manager tests/test_configuration_manager.cpp ${COMMON_SOURCES}
    src/packager/configuration_loader.cpp
    src/packager/configuration_validator.cpp
    src/packager/configuration_manager.cpp
)
target_link_libraries(test_configuration_manager ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_configuration_manager PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_configuration_manager)

# Create test executable for packager integration
add_executable(test_packager_integration tests/test_packager_integration.cpp ${COMMON_SOURCES}
    src/packager/configuration_loader.cpp
    src/packager/configuration_validator.cpp
    src/packager/configuration_manager.cpp
    src/packager/folder_scanner.cpp
    src/packager/compression_module.cpp
    src/packager/metadata_generator.cpp
)
target_link_libraries(test_packager_integration ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_packager_integration PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_packager_integration)

# Create test executable for path resolver
add_executable(test_path_resolver tests/test_path_resolver.cpp ${COMMON_SOURCES}
    src/installer/path_resolver.cpp
)
target_link_libraries(test_path_resolver ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_path_resolver PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_path_resolver)

# Create test executable for installer extensions
add_executable(test_installer_extensions tests/test_installer_extensions.cpp ${COMMON_SOURCES}
    src/installer/metadata_parser.cpp
    src/installer/path_resolver.cpp
)
target_link_libraries(test_installer_extensions ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_installer_extensions PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_installer_extensions)

# Create test executable for end-to-end workflow
add_executable(test_end_to_end_workflow tests/test_end_to_end_workflow.cpp ${COMMON_SOURCES}
    src/packager/configuration_loader.cpp
    src/packager/configuration_validator.cpp
    src/packager/configuration_manager.cpp
    src/packager/folder_scanner.cpp
    src/packager/compression_module.cpp
    src/packager/metadata_generator.cpp
    src/packager/installer_generator.cpp
    src/installer/metadata_parser.cpp
    src/installer/path_resolver.cpp
)
target_link_libraries(test_end_to_end_workflow ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_end_to_end_workflow PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_end_to_end_workflow)

# Create full compression test executable
add_executable(test_full_compression test_full_compression.cpp ${COMMON_SOURCES}
    src/packager/compression_module.cpp
)
target_link_libraries(test_full_compression ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_full_compression PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_full_compression)

# Create ZSTD block compression test executable
add_executable(test_zstd_blocks test_zstd_blocks.cpp ${COMMON_SOURCES}
    src/packager/compression_module.cpp
    src/installer/decompression_engine.cpp
    src/installer/thread_pool_manager.cpp
    src/installer/file_system_operator.cpp
)
target_link_libraries(test_zstd_blocks ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_zstd_blocks PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_zstd_blocks)

# Create LZMA block compression test executable
add_executable(test_lzma_blocks test_lzma_blocks.cpp ${COMMON_SOURCES}
    src/packager/compression_module.cpp
    src/installer/decompression_engine.cpp
    src/installer/thread_pool_manager.cpp
    src/installer/file_system_operator.cpp
)
target_link_libraries(test_lzma_blocks ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(test_lzma_blocks PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(test_lzma_blocks)

# Create installer diagnostics tool
add_executable(diagnose_installer diagnose_installer.cpp ${COMMON_SOURCES}
    src/installer/metadata_parser.cpp
)
target_link_libraries(diagnose_installer ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} Threads::Threads)
target_include_directories(diagnose_installer PRIVATE ${ZSTD_INCLUDE_DIRS} ${LIBLZMA_INCLUDE_DIRS})
copy_dlls_to_target(diagnose_installer)

add_test(NAME FolderScannerTests COMMAND test_folder_scanner)
add_test(NAME CompressionModuleTests COMMAND test_compression_module)
add_test(NAME MetadataGeneratorTests COMMAND test_metadata_generator)
add_test(NAME DecompressionEngineTests COMMAND test_decompression_engine)
add_test(NAME FileSystemOperatorTests COMMAND test_file_system_operator)
add_test(NAME MetadataManagementTests COMMAND test_metadata_management)
add_test(NAME InstallerGeneratorTests COMMAND test_installer_generator)
add_test(NAME ThreadPoolManagerTests COMMAND test_thread_pool_manager)
add_test(NAME ConsoleInterfaceTests COMMAND test_console_interface)
add_test(NAME ConfigurationStructuresTests COMMAND test_configuration_structures)
add_test(NAME ConfigurationLoaderTests COMMAND test_configuration_loader)
add_test(NAME ConfigurationValidatorTests COMMAND test_configuration_validator)
add_test(NAME ConfigurationLoaderPBTTests COMMAND test_configuration_loader_pbt)
add_test(NAME ConfigurationValidatorPBTTests COMMAND test_configuration_validator_pbt)
add_test(NAME ConfigurationManagerTests COMMAND test_configuration_manager)
add_test(NAME PackagerIntegrationTests COMMAND test_packager_integration)
add_test(NAME PackagerIntegrationPBTTests COMMAND test_packager_integration_pbt)
add_test(NAME ThreadPoolPBTTests COMMAND test_thread_pool_pbt)
add_test(NAME DecompressionErrorRecoveryTests COMMAND test_decompression_error_recovery)
add_test(NAME FileSystemOperatorPBTTests COMMAND test_file_system_operator_pbt)
add_test(NAME StatusReportAccuracyTests COMMAND test_status_report_accuracy)
add_test(NAME PathResolverTests COMMAND test_path_resolver)
add_test(NAME PathResolverPBTTests COMMAND test_path_resolver_pbt)
add_test(NAME EndToEndWorkflowTests COMMAND test_end_to_end_workflow)