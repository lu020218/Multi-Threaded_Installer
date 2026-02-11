#include "common/utf8_utils.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <tchar.h>
#endif

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using MultiThreadedInstaller::AcpToUtf8;
using MultiThreadedInstaller::MultiByteToWide;
using MultiThreadedInstaller::PathFromTChar;
using MultiThreadedInstaller::PathFromUtf8;
using MultiThreadedInstaller::TCharToWide;
using MultiThreadedInstaller::Utf8FromPath;
using MultiThreadedInstaller::Utf8ToWide;
using MultiThreadedInstaller::WideToMultiByte;
using MultiThreadedInstaller::WideToUtf8;

void AssertTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestUtf8RoundTrip() {
    const std::string utf8 = u8"Hello-\u6D4B\u8BD5-\u20AC";
    const std::wstring wide = Utf8ToWide(utf8);
    AssertTrue(!wide.empty(), "Utf8ToWide should decode valid UTF-8");
    AssertTrue(WideToUtf8(wide) == utf8, "WideToUtf8 should round-trip valid UTF-8");
}

void TestUtf8InvalidInput() {
    const std::string invalidUtf8 = "\xE4\xB8";
    AssertTrue(Utf8ToWide(invalidUtf8).empty(), "Utf8ToWide should reject truncated UTF-8");

#ifdef _WIN32
    const std::wstring invalidWide = MultiByteToWide(invalidUtf8, CP_UTF8, MB_ERR_INVALID_CHARS);
    AssertTrue(invalidWide.empty(), "MultiByteToWide should reject invalid UTF-8 when MB_ERR_INVALID_CHARS is set");
#endif
}

void TestGenericConversionOverloads() {
#ifdef _WIN32
    const char withNull[] = {'A', '\0', 'B'};
    const std::wstring wideEmbeddedNull = MultiByteToWide(withNull, 3, CP_UTF8, 0);
    AssertTrue(wideEmbeddedNull.size() == 3, "MultiByteToWide(length) should preserve embedded NUL");
    AssertTrue(wideEmbeddedNull[0] == L'A' && wideEmbeddedNull[1] == L'\0' && wideEmbeddedNull[2] == L'B',
               "MultiByteToWide(length) content mismatch");

    const std::string utf8 = WideToMultiByte(wideEmbeddedNull.data(), 3, CP_UTF8, 0);
    AssertTrue(utf8.size() == 3, "WideToMultiByte(length) should preserve embedded NUL");
    AssertTrue(utf8[0] == 'A' && utf8[1] == '\0' && utf8[2] == 'B',
               "WideToMultiByte(length) content mismatch");

    const std::wstring fromCStr = MultiByteToWide("ABC", -1, CP_UTF8, 0);
    AssertTrue(fromCStr == L"ABC", "MultiByteToWide(-1) should trim trailing NUL");
#endif
}

void TestAcpBridgeConsistency() {
#ifdef _WIN32
    std::string acpBytes;
    acpBytes.push_back('A');
    acpBytes.push_back(static_cast<char>(0x80));
    acpBytes.push_back(static_cast<char>(0xFF));

    const std::string viaBridge = AcpToUtf8(acpBytes);
    const std::string viaManual = WideToUtf8(MultiByteToWide(acpBytes, CP_ACP, 0));
    AssertTrue(viaBridge == viaManual, "AcpToUtf8 should match manual ACP->Wide->UTF8 conversion");
    AssertTrue(AcpToUtf8("").empty(), "AcpToUtf8 should return empty for empty input");
#else
    AssertTrue(AcpToUtf8("abc") == "abc", "AcpToUtf8 should passthrough on non-Windows");
#endif
}

void TestPathHelpers() {
    const std::string utf8Path = u8"C:\\Temp\\\u6D4B\u8BD5 Folder\\app.exe";
    const std::filesystem::path nativePath = PathFromUtf8(utf8Path);
    AssertTrue(Utf8FromPath(nativePath) == utf8Path, "Path UTF-8 round-trip mismatch");

#ifdef _WIN32
    const TCHAR* text = _T("C:\\Temp\\UnitTest");
    AssertTrue(TCharToWide(text) == L"C:\\Temp\\UnitTest", "TCharToWide mismatch");
    AssertTrue(PathFromTChar(text) == std::filesystem::path(L"C:\\Temp\\UnitTest"),
               "PathFromTChar mismatch");
#endif
}

} // namespace

int main() {
    try {
        TestUtf8RoundTrip();
        TestUtf8InvalidInput();
        TestGenericConversionOverloads();
        TestAcpBridgeConsistency();
        TestPathHelpers();
        std::cout << "utf8_utils test passed" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "utf8_utils test failed: " << ex.what() << std::endl;
        return 1;
    }
}
