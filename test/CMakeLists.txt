cmake_minimum_required(VERSION 3.16)

add_executable(FileLockSimulator FileLockSimulator.cpp)
target_compile_features(FileLockSimulator PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(FileLockSimulator PRIVATE /W4 /EHsc)
endif()

add_executable(SchemaRegressionTests
    schema_regression_tests.cpp
    ../src/packager/config_value_reader.cpp
    ../src/packager/config_parse_collections.cpp
    ../src/packager/configuration_loader.cpp
    ../src/packager/configuration_validator.cpp
    ../src/packager/configuration_manager.cpp
    ../src/packager/metadata_generator.cpp
    ../src/installer/metadata_parser.cpp
    ../src/installer/metadata_binary_reader.cpp
    ../src/installer/path_resolver.cpp
    ../src/installer/installer_helpers.cpp
    ../src/installer/file_io_utils.cpp
    ../src/installer/file_system_operator.cpp
    ../src/installer/registry_utils.cpp
    ../src/installer/install_plan_builder.cpp
    ../src/installer/install_manifest_store.cpp
    ../src/installer/installed_instance_resolver.cpp
    ../src/installer/install_state_utils.cpp
    ../src/installer/console_interface.cpp
    ../src/installer/shortcut_startup_utils.cpp
    ../src/installer/self_delete_scheduler.cpp
    ../src/installer/upgrade_cleanup.cpp
    ../src/installer/uninstall_manager.cpp
    ../src/common/types.cpp
    ../src/common/utf8_utils.cpp
    ../src/common/version_utils.cpp
    ../src/common/installer_logger.cpp
)

target_compile_features(SchemaRegressionTests PRIVATE cxx_std_17)
target_link_libraries(SchemaRegressionTests PRIVATE ${YAML_CPP_LIBRARIES})
target_include_directories(SchemaRegressionTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${THIRD_PARTY_DIR}/nlohmann
    ${YAML_CPP_INCLUDE_DIR}
)

if(MSVC)
    target_compile_options(SchemaRegressionTests PRIVATE /W4 /EHsc)
    target_compile_definitions(SchemaRegressionTests PRIVATE YAML_CPP_STATIC_DEFINE UNICODE _UNICODE)
endif()

if(WIN32)
    target_link_libraries(SchemaRegressionTests PRIVATE ole32 shell32 dbghelp)
endif()

add_test(NAME SchemaRegressionTests COMMAND SchemaRegressionTests)
