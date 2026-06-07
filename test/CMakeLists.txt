cmake_minimum_required(VERSION 3.16)

add_executable(FileLockSimulator FileLockSimulator.cpp)
target_compile_features(FileLockSimulator PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(FileLockSimulator PRIVATE /W4 /EHsc)
endif()

if(NOT TARGET spdlog_header_only)
    set(SPDLOG_INCLUDE_DIR "${THIRD_PARTY_DIR}/spdlog/include")
    add_library(spdlog_header_only INTERFACE)
    target_include_directories(spdlog_header_only INTERFACE "${SPDLOG_INCLUDE_DIR}")
    target_compile_definitions(spdlog_header_only INTERFACE
        SPDLOG_WCHAR_FILENAMES
    )
endif()

# NOTE: SchemaRegressionTests 针对旧 schema（installer/uninstaller 块），已随重构淘汰。
# 重构后的 app/package/hooks schema 需要新的回归测试，待补。
