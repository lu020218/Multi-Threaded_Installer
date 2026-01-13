#include <iostream>
#include <cassert>
#include <filesystem>
#include <windows.h>
#include "installer/path_resolver.h"

namespace fs = std::filesystem;
using namespace MultiThreadedInstaller;

// 测试辅助函数
void assert_true(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        exit(1);
    }
    std::cout << "PASSED: " << message << std::endl;
}

void assert_equal(const std::string& expected, const std::string& actual, const std::string& message) {
    if (expected != actual) {
        std::cerr << "FAILED: " << message << std::endl;
        std::cerr << "  Expected: " << expected << std::endl;
        std::cerr << "  Actual: " << actual << std::endl;
        exit(1);
    }
    std::cout << "PASSED: " << message << std::endl;
}

void assert_not_equal(const std::string& notExpected, const std::string& actual, const std::string& message) {
    if (notExpected == actual) {
        std::cerr << "FAILED: " << message << std::endl;
        std::cerr << "  Should not be: " << notExpected << std::endl;
        exit(1);
    }
    std::cout << "PASSED: " << message << std::endl;
}

void assert_contains(const std::string& haystack, const std::string& needle, const std::string& message) {
    if (haystack.find(needle) == std::string::npos) {
        std::cerr << "FAILED: " << message << std::endl;
        std::cerr << "  String: " << haystack << std::endl;
        std::cerr << "  Should contain: " << needle << std::endl;
        exit(1);
    }
    std::cout << "PASSED: " << message << std::endl;
}

void assert_not_contains(const std::string& haystack, const std::string& needle, const std::string& message) {
    if (haystack.find(needle) != std::string::npos) {
        std::cerr << "FAILED: " << message << std::endl;
        std::cerr << "  String: " << haystack << std::endl;
        std::cerr << "  Should not contain: " << needle << std::endl;
        exit(1);
    }
    std::cout << "PASSED: " << message << std::endl;
}

// 测试环境变量展开
void test_expand_environment_variables() {
    std::cout << "\n=== Testing expandEnvironmentVariables ===" << std::endl;
    
    InstallerPathResolver resolver;
    
    // 测试 %ProgramFiles%
    {
        std::string path = "%ProgramFiles%\\MyApp";
        std::string expanded = resolver.expandEnvironmentVariables(path);
        assert_not_contains(expanded, "%ProgramFiles%", "ProgramFiles should be expanded");
    }
    
    // 测试 %AppData%
    {
        std::string path = "%AppData%\\MyApp";
        std::string expanded = resolver.expandEnvironmentVariables(path);
        assert_not_contains(expanded, "%AppData%", "AppData should be expanded");
    }
    
    // 测试 %LocalAppData%
    {
        std::string path = "%LocalAppData%\\MyApp";
        std::string expanded = resolver.expandEnvironmentVariables(path);
        assert_not_contains(expanded, "%LocalAppData%", "LocalAppData should be expanded");
    }
    
    // 测试 %ProgramData%
    {
        std::string path = "%ProgramData%\\MyApp";
        std::string expanded = resolver.expandEnvironmentVariables(path);
        assert_not_contains(expanded, "%ProgramData%", "ProgramData should be expanded");
    }
    
    // 测试没有环境变量的路径
    {
        std::string path = "C:\\MyApp\\Folder";
        std::string expanded = resolver.expandEnvironmentVariables(path);
        assert_equal(path, expanded, "Path without variables should remain unchanged");
    }
    
    // 测试空路径
    {
        std::string path = "";
        std::string expanded = resolver.expandEnvironmentVariables(path);
        assert_equal("", expanded, "Empty path should remain empty");
    }
}

// 测试应用程序名检测
void test_path_contains_app_name() {
    std::cout << "\n=== Testing pathContainsAppName ===" << std::endl;
    
    InstallerPathResolver resolver;
    
    // 测试精确匹配
    {
        std::string path = "C:\\Program Files\\MyApplication";
        std::string appName = "MyApplication";
        assert_true(resolver.pathContainsAppName(path, appName), 
                   "Should detect exact match");
    }
    
    // 测试不区分大小写
    {
        std::string path = "C:\\Program Files\\myapplication";
        std::string appName = "MyApplication";
        assert_true(resolver.pathContainsAppName(path, appName), 
                   "Should detect case-insensitive match");
    }
    
    // 测试不存在
    {
        std::string path = "C:\\Program Files\\SomeOtherFolder";
        std::string appName = "MyApplication";
        assert_true(!resolver.pathContainsAppName(path, appName), 
                   "Should not detect when app name is not present");
    }
    
    // 测试带尾部斜杠
    {
        std::string path = "C:\\Program Files\\MyApplication\\";
        std::string appName = "MyApplication";
        assert_true(resolver.pathContainsAppName(path, appName), 
                   "Should detect with trailing slash");
    }
    
    // 测试空路径
    {
        std::string path = "";
        std::string appName = "MyApplication";
        assert_true(!resolver.pathContainsAppName(path, appName), 
                   "Empty path should return false");
    }
    
    // 测试空应用程序名
    {
        std::string path = "C:\\Program Files\\MyApplication";
        std::string appName = "";
        assert_true(!resolver.pathContainsAppName(path, appName), 
                   "Empty app name should return false");
    }
}

// 测试路径补齐逻辑
void test_append_app_name_if_needed() {
    std::cout << "\n=== Testing appendAppNameIfNeeded ===" << std::endl;
    
    InstallerPathResolver resolver;
    
    // 测试不存在时追加
    {
        std::string basePath = "C:\\Program Files";
        std::string appName = "MyApplication";
        std::string result = resolver.appendAppNameIfNeeded(basePath, appName);
        assert_equal("C:\\Program Files\\MyApplication", result, 
                    "Should append app name when not present");
    }
    
    // 测试已存在时不追加
    {
        std::string basePath = "C:\\Program Files\\MyApplication";
        std::string appName = "MyApplication";
        std::string result = resolver.appendAppNameIfNeeded(basePath, appName);
        assert_equal("C:\\Program Files\\MyApplication", result, 
                    "Should not append when already present");
    }
    
    // 测试带尾部斜杠
    {
        std::string basePath = "C:\\Program Files\\";
        std::string appName = "MyApplication";
        std::string result = resolver.appendAppNameIfNeeded(basePath, appName);
        assert_equal("C:\\Program Files\\MyApplication", result, 
                    "Should handle trailing slash correctly");
    }
    
    // 测试不区分大小写
    {
        std::string basePath = "C:\\Program Files\\myapplication";
        std::string appName = "MyApplication";
        std::string result = resolver.appendAppNameIfNeeded(basePath, appName);
        assert_equal("C:\\Program Files\\myapplication", result, 
                    "Should not append when present (case-insensitive)");
    }
    
    // 测试空路径
    {
        std::string basePath = "";
        std::string appName = "MyApplication";
        std::string result = resolver.appendAppNameIfNeeded(basePath, appName);
        assert_equal("", result, "Empty path should remain empty");
    }
    
    // 测试空应用程序名
    {
        std::string basePath = "C:\\Program Files";
        std::string appName = "";
        std::string result = resolver.appendAppNameIfNeeded(basePath, appName);
        assert_equal("C:\\Program Files", result, 
                    "Empty app name should not modify path");
    }
}

// 测试不重复添加应用程序名
void test_do_not_duplicate_app_name() {
    std::cout << "\n=== Testing No Duplication ===" << std::endl;
    
    InstallerPathResolver resolver;
    
    // 测试精确匹配
    {
        std::string basePath = "D:\\MyApps\\MyApplication";
        std::string appName = "MyApplication";
        std::string result = resolver.appendAppNameIfNeeded(basePath, appName);
        assert_equal("D:\\MyApps\\MyApplication", result, 
                    "Should not duplicate exact match");
        assert_not_contains(result, "MyApplication\\MyApplication", 
                          "Should not contain duplicated app name");
    }
    
    // 测试不同大小写
    {
        std::string basePath = "D:\\MyApps\\MYAPPLICATION";
        std::string appName = "MyApplication";
        std::string result = resolver.appendAppNameIfNeeded(basePath, appName);
        assert_equal("D:\\MyApps\\MYAPPLICATION", result, 
                    "Should not duplicate case-insensitive match");
    }
}

// 测试完整的路径解析
void test_resolve_final_path() {
    std::cout << "\n=== Testing resolveFinalPath ===" << std::endl;
    
    InstallerPathResolver resolver;
    
    // 测试安装目录 - 不含应用程序名
    {
        std::string userPath = "D:\\Program Files";
        std::string appName = "MyApplication";
        std::string result = resolver.resolveFinalPath(
            userPath, 
            SpecialDirectoryType::INSTALL_DIRECTORY, 
            appName
        );
        assert_equal("D:\\Program Files\\MyApplication", result, 
                    "Should append app name to install directory");
    }
    
    // 测试安装目录 - 已含应用程序名
    {
        std::string userPath = "D:\\Program Files\\MyApplication";
        std::string appName = "MyApplication";
        std::string result = resolver.resolveFinalPath(
            userPath, 
            SpecialDirectoryType::INSTALL_DIRECTORY, 
            appName
        );
        assert_equal("D:\\Program Files\\MyApplication", result, 
                    "Should not duplicate app name in install directory");
    }
    
    // 测试 AppData Roaming
    {
        std::string userPath = ""; // 不使用用户路径
        std::string appName = "MyApplication";
        std::string result = resolver.resolveFinalPath(
            userPath, 
            SpecialDirectoryType::APPDATA_ROAMING, 
            appName
        );
        assert_contains(result, "MyApplication", 
                       "Should contain app name for AppData Roaming");
    }
    
    // 测试 ProgramData
    {
        std::string userPath = ""; // 不使用用户路径
        std::string appName = "MyApplication";
        std::string result = resolver.resolveFinalPath(
            userPath, 
            SpecialDirectoryType::PROGRAM_DATA, 
            appName
        );
        assert_contains(result, "MyApplication", 
                       "Should contain app name for ProgramData");
    }
    
    // 测试带多个尾部斜杠
    {
        std::string userPath = "D:\\Program Files\\\\\\";
        std::string appName = "MyApplication";
        std::string result = resolver.resolveFinalPath(
            userPath, 
            SpecialDirectoryType::INSTALL_DIRECTORY, 
            appName
        );
        assert_equal("D:\\Program Files\\MyApplication", result, 
                    "Should normalize path with multiple trailing slashes");
    }
}

int main() {
    std::cout << "Running InstallerPathResolver Tests..." << std::endl;
    
    try {
        test_expand_environment_variables();
        test_path_contains_app_name();
        test_append_app_name_if_needed();
        test_do_not_duplicate_app_name();
        test_resolve_final_path();
        
        std::cout << "\n=== All tests passed! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
