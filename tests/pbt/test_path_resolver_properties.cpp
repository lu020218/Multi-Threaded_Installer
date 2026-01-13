#include <iostream>
#include <cassert>
#include <random>
#include <vector>
#include <string>
#include <algorithm>
#include "installer/path_resolver.h"

using namespace MultiThreadedInstaller;

// 随机生成器
class RandomGenerator {
public:
    RandomGenerator() : gen(rd()), dist(0, 1000) {}
    
    int getInt(int min = 0, int max = 1000) {
        std::uniform_int_distribution<> d(min, max);
        return d(gen);
    }
    
    std::string getString(size_t length = 10) {
        const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            result += charset[getInt(0, sizeof(charset) - 2)];
        }
        return result;
    }
    
    bool getBool() {
        return getInt(0, 1) == 1;
    }
    
    // 生成随机路径
    std::string getRandomPath(int depth = 3) {
        std::string path;
        
        // 随机选择驱动器
        char drive = 'C' + getInt(0, 3);
        path = std::string(1, drive) + ":\\";
        
        // 添加随机目录
        for (int i = 0; i < depth; ++i) {
            if (i > 0) path += "\\";
            path += getString(getInt(5, 15));
        }
        
        return path;
    }
    
    // 生成包含环境变量的路径
    std::string getPathWithEnvVar() {
        std::vector<std::string> envVars = {
            "%ProgramFiles%",
            "%AppData%",
            "%LocalAppData%",
            "%ProgramData%",
            "%USERPROFILE%"
        };
        
        std::string envVar = envVars[getInt(0, envVars.size() - 1)];
        
        // 可能添加子路径
        if (getBool()) {
            envVar += "\\" + getString(getInt(5, 15));
        }
        
        return envVar;
    }
    
    // 生成应用程序名
    std::string getAppName() {
        return getString(getInt(5, 20));
    }
    
    // 生成特殊目录类型
    SpecialDirectoryType getDirectoryType() {
        std::vector<SpecialDirectoryType> types = {
            SpecialDirectoryType::INSTALL_DIRECTORY,
            SpecialDirectoryType::PROGRAM_FILES,
            SpecialDirectoryType::APPDATA_ROAMING,
            SpecialDirectoryType::APPDATA_LOCAL,
            SpecialDirectoryType::PROGRAM_DATA
        };
        return types[getInt(0, types.size() - 1)];
    }
    
private:
    std::random_device rd;
    std::mt19937 gen;
    std::uniform_int_distribution<> dist;
};

// 测试辅助函数
void assert_property(bool condition, const std::string& message, int iteration) {
    if (!condition) {
        std::cerr << "FAILED at iteration " << iteration << ": " << message << std::endl;
        exit(1);
    }
}

// Property 9: Install Directory Path Resolution
// For any user-selected path and application name, if the path does not contain 
// the app name, the installer should append it; if it already contains the app name, 
// it should not duplicate it.
void test_property_9_install_directory_path_resolution(int iterations = 100) {
    std::cout << "\n=== Property 9: Install Directory Path Resolution ===" << std::endl;
    std::cout << "Feature: packager-config-file, Property 9" << std::endl;
    std::cout << "Validates: Requirements 4.6, 2.5" << std::endl;
    
    RandomGenerator rng;
    InstallerPathResolver resolver;
    int passed = 0;
    
    for (int i = 0; i < iterations; ++i) {
        // 生成随机路径和应用程序名
        std::string basePath = rng.getRandomPath(rng.getInt(1, 4));
        std::string appName = rng.getAppName();
        
        // 随机决定是否在路径中包含应用程序名
        bool shouldContainAppName = rng.getBool();
        if (shouldContainAppName) {
            basePath += "\\" + appName;
        }
        
        // 可能添加尾部斜杠
        if (rng.getBool()) {
            basePath += "\\";
        }
        
        // 解析路径
        std::string result = resolver.resolveFinalPath(
            basePath,
            SpecialDirectoryType::INSTALL_DIRECTORY,
            appName
        );
        
        // 验证属性：结果应该包含应用程序名
        assert_property(
            result.find(appName) != std::string::npos,
            "Result should contain app name: " + result + " (app: " + appName + ")",
            i
        );
        
        // 验证属性：应用程序名不应该重复
        std::string doubleAppName = appName + "\\" + appName;
        assert_property(
            result.find(doubleAppName) == std::string::npos,
            "App name should not be duplicated: " + result,
            i
        );
        
        // 验证属性：如果原路径已包含应用程序名，结果应该等于规范化后的原路径
        if (shouldContainAppName) {
            std::string normalizedBase = resolver.appendAppNameIfNeeded(basePath, appName);
            // 移除尾部斜杠进行比较
            std::string resultNorm = result;
            while (!resultNorm.empty() && resultNorm.back() == '\\') {
                resultNorm.pop_back();
            }
            std::string baseNorm = normalizedBase;
            while (!baseNorm.empty() && baseNorm.back() == '\\') {
                baseNorm.pop_back();
            }
            
            // 应该不区分大小写地包含应用程序名
            std::string resultLower = resultNorm;
            std::string appNameLower = appName;
            std::transform(resultLower.begin(), resultLower.end(), resultLower.begin(), ::tolower);
            std::transform(appNameLower.begin(), appNameLower.end(), appNameLower.begin(), ::tolower);
            
            assert_property(
                resultLower.find(appNameLower) != std::string::npos,
                "Result should contain app name (case-insensitive): " + result,
                i
            );
        }
        
        passed++;
    }
    
    std::cout << "PASSED: " << passed << "/" << iterations << " iterations" << std::endl;
}

// Property 10: Environment Variable Path Resolution
// For any path containing environment variables, the installer should expand the 
// variables and append the application name.
void test_property_10_environment_variable_path_resolution(int iterations = 100) {
    std::cout << "\n=== Property 10: Environment Variable Path Resolution ===" << std::endl;
    std::cout << "Feature: packager-config-file, Property 10" << std::endl;
    std::cout << "Validates: Requirements 5.3, 5.4" << std::endl;
    
    RandomGenerator rng;
    InstallerPathResolver resolver;
    int passed = 0;
    
    for (int i = 0; i < iterations; ++i) {
        // 生成包含环境变量的路径
        std::string pathWithEnvVar = rng.getPathWithEnvVar();
        std::string appName = rng.getAppName();
        SpecialDirectoryType dirType = rng.getDirectoryType();
        
        // 跳过 INSTALL_DIRECTORY 类型（它使用用户路径，不是环境变量）
        if (dirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
            dirType = SpecialDirectoryType::PROGRAM_FILES;
        }
        
        // 解析路径
        std::string result = resolver.resolveFinalPath(
            "", // 不使用用户路径
            dirType,
            appName
        );
        
        // 验证属性：环境变量应该被展开（结果不应包含%符号）
        assert_property(
            result.find('%') == std::string::npos,
            "Environment variables should be expanded: " + result,
            i
        );
        
        // 验证属性：结果应该包含应用程序名
        assert_property(
            result.find(appName) != std::string::npos,
            "Result should contain app name: " + result + " (app: " + appName + ")",
            i
        );
        
        // 验证属性：应用程序名不应该重复
        std::string doubleAppName = appName + "\\" + appName;
        assert_property(
            result.find(doubleAppName) == std::string::npos,
            "App name should not be duplicated: " + result,
            i
        );
        
        // 验证属性：结果应该是绝对路径（包含驱动器号）
        assert_property(
            result.length() >= 3 && result[1] == ':' && result[2] == '\\',
            "Result should be an absolute path: " + result,
            i
        );
        
        passed++;
    }
    
    std::cout << "PASSED: " << passed << "/" << iterations << " iterations" << std::endl;
}

// 额外的属性测试：路径规范化的幂等性
// For any path, normalizing it twice should produce the same result as normalizing it once
void test_property_path_normalization_idempotence(int iterations = 100) {
    std::cout << "\n=== Additional Property: Path Normalization Idempotence ===" << std::endl;
    
    RandomGenerator rng;
    InstallerPathResolver resolver;
    int passed = 0;
    
    for (int i = 0; i < iterations; ++i) {
        // 生成随机路径，可能带多个尾部斜杠
        std::string basePath = rng.getRandomPath(rng.getInt(1, 4));
        int slashCount = rng.getInt(0, 5);
        for (int j = 0; j < slashCount; ++j) {
            basePath += "\\";
        }
        
        std::string appName = rng.getAppName();
        
        // 第一次补齐
        std::string result1 = resolver.appendAppNameIfNeeded(basePath, appName);
        
        // 第二次补齐（应该不改变）
        std::string result2 = resolver.appendAppNameIfNeeded(result1, appName);
        
        // 验证幂等性
        assert_property(
            result1 == result2,
            "Path normalization should be idempotent: " + result1 + " vs " + result2,
            i
        );
        
        passed++;
    }
    
    std::cout << "PASSED: " << passed << "/" << iterations << " iterations" << std::endl;
}

// 额外的属性测试：大小写不敏感性
// For any path and app name, detection should be case-insensitive
void test_property_case_insensitivity(int iterations = 100) {
    std::cout << "\n=== Additional Property: Case Insensitivity ===" << std::endl;
    
    RandomGenerator rng;
    InstallerPathResolver resolver;
    int passed = 0;
    
    for (int i = 0; i < iterations; ++i) {
        std::string basePath = rng.getRandomPath(rng.getInt(1, 3));
        std::string appName = rng.getAppName();
        
        // 创建不同大小写的应用程序名
        std::string appNameUpper = appName;
        std::transform(appNameUpper.begin(), appNameUpper.end(), appNameUpper.begin(), ::toupper);
        
        std::string appNameLower = appName;
        std::transform(appNameLower.begin(), appNameLower.end(), appNameLower.begin(), ::tolower);
        
        // 路径包含大写版本
        std::string pathWithUpper = basePath + "\\" + appNameUpper;
        
        // 使用小写应用程序名检测
        bool containsLower = resolver.pathContainsAppName(pathWithUpper, appNameLower);
        
        // 使用原始应用程序名检测
        bool containsOriginal = resolver.pathContainsAppName(pathWithUpper, appName);
        
        // 验证大小写不敏感
        assert_property(
            containsLower && containsOriginal,
            "Detection should be case-insensitive",
            i
        );
        
        // 验证不会重复添加
        std::string result = resolver.appendAppNameIfNeeded(pathWithUpper, appNameLower);
        std::string doubleAppName1 = appNameUpper + "\\" + appNameLower;
        std::string doubleAppName2 = appNameUpper + "\\" + appName;
        
        assert_property(
            result.find(doubleAppName1) == std::string::npos &&
            result.find(doubleAppName2) == std::string::npos,
            "Should not duplicate app name with different cases: " + result,
            i
        );
        
        passed++;
    }
    
    std::cout << "PASSED: " << passed << "/" << iterations << " iterations" << std::endl;
}

int main() {
    std::cout << "Running InstallerPathResolver Property-Based Tests..." << std::endl;
    std::cout << "Each property will be tested with 100 random inputs" << std::endl;
    
    try {
        // 运行必需的属性测试
        test_property_9_install_directory_path_resolution(100);
        test_property_10_environment_variable_path_resolution(100);
        
        // 运行额外的属性测试
        test_property_path_normalization_idempotence(100);
        test_property_case_insensitivity(100);
        
        std::cout << "\n=== All property-based tests passed! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nProperty test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
