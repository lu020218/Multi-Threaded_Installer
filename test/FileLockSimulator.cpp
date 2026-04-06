#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>

struct Options {
    std::wstring mode = L"open";      // open | lock | both
    std::wstring path;
    std::wstring share = L"none";     // none | r | rw | rwd
    uint64_t offset = 0;
    uint64_t length = 0;              // 0 means whole file from offset to end, or default 1 byte for empty file
    bool create_if_missing = true;
    bool write_test_data = false;
};

static void PrintUsage() {
    std::wcout << L"FileLockSimulator - simulate file in-use / locked states for installer testing\n\n";
    std::wcout << L"Usage:\n";
    std::wcout << L"  FileLockSimulator.exe --mode=<open|lock|both> --path=<file>\n";
    std::wcout << L"                        [--share=<none|r|rw|rwd>]\n";
    std::wcout << L"                        [--offset=<n>] [--length=<n>]\n";
    std::wcout << L"                        [--create-if-missing=<0|1>]\n";
    std::wcout << L"                        [--write-test-data=<0|1>]\n\n";

    std::wcout << L"Examples:\n";
    std::wcout << L"  FileLockSimulator.exe --mode=open --path=\"C:\\Test\\a.dll\" --share=none\n";
    std::wcout << L"  FileLockSimulator.exe --mode=lock --path=\"C:\\Test\\a.dll\" --offset=0 --length=1024\n";
    std::wcout << L"  FileLockSimulator.exe --mode=both --path=\"C:\\Test\\a.dll\" --share=none --offset=0 --length=4096\n";
}

static bool StartsWith(const std::wstring& s, const std::wstring& prefix) {
    return s.rfind(prefix, 0) == 0;
}

static std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(towlower(c));
    });
    return s;
}

static bool ParseUInt64(const std::wstring& s, uint64_t& out) {
    if (s.empty()) return false;
    wchar_t* end = nullptr;
    unsigned long long v = wcstoull(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != L'\0') return false;
    out = static_cast<uint64_t>(v);
    return true;
}

static bool ParseBool01(const std::wstring& s, bool& out) {
    if (s == L"1") {
        out = true;
        return true;
    }
    if (s == L"0") {
        out = false;
        return true;
    }
    return false;
}

static bool ParseArgs(int argc, wchar_t* argv[], Options& opt) {
    if (argc <= 1) {
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];

        if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            return false;
        }

        if (!StartsWith(arg, L"--")) {
            std::wcerr << L"Invalid argument: " << arg << L"\n";
            return false;
        }

        size_t eq = arg.find(L'=');
        if (eq == std::wstring::npos) {
            std::wcerr << L"Invalid argument format: " << arg << L"\n";
            return false;
        }

        std::wstring key = ToLower(arg.substr(2, eq - 2));
        std::wstring value = arg.substr(eq + 1);

        if (key == L"mode") {
            opt.mode = ToLower(value);
            if (opt.mode != L"open" && opt.mode != L"lock" && opt.mode != L"both") {
                std::wcerr << L"Invalid mode: " << value << L"\n";
                return false;
            }
        } else if (key == L"path") {
            opt.path = value;
        } else if (key == L"share") {
            opt.share = ToLower(value);
            if (opt.share != L"none" && opt.share != L"r" && opt.share != L"rw" && opt.share != L"rwd") {
                std::wcerr << L"Invalid share: " << value << L"\n";
                return false;
            }
        } else if (key == L"offset") {
            if (!ParseUInt64(value, opt.offset)) {
                std::wcerr << L"Invalid offset: " << value << L"\n";
                return false;
            }
        } else if (key == L"length") {
            if (!ParseUInt64(value, opt.length)) {
                std::wcerr << L"Invalid length: " << value << L"\n";
                return false;
            }
        } else if (key == L"create-if-missing") {
            if (!ParseBool01(value, opt.create_if_missing)) {
                std::wcerr << L"Invalid create-if-missing: " << value << L"\n";
                return false;
            }
        } else if (key == L"write-test-data") {
            if (!ParseBool01(value, opt.write_test_data)) {
                std::wcerr << L"Invalid write-test-data: " << value << L"\n";
                return false;
            }
        } else {
            std::wcerr << L"Unknown argument: " << key << L"\n";
            return false;
        }
    }

    if (opt.path.empty()) {
        std::wcerr << L"--path is required\n";
        return false;
    }

    return true;
}

static DWORD BuildShareMode(const std::wstring& share) {
    if (share == L"none") return 0;

    DWORD mode = 0;
    if (share.find(L'r') != std::wstring::npos) {
        mode |= FILE_SHARE_READ;
    }
    if (share.find(L'w') != std::wstring::npos) {
        mode |= FILE_SHARE_WRITE;
    }
    if (share.find(L'd') != std::wstring::npos) {
        mode |= FILE_SHARE_DELETE;
    }
    return mode;
}

static bool EnsureFileExists(const Options& opt) {
    DWORD creation = opt.create_if_missing ? OPEN_ALWAYS : OPEN_EXISTING;

    HANDLE h = CreateFileW(
        opt.path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        creation,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (h == INVALID_HANDLE_VALUE) {
        std::wcerr << L"EnsureFileExists failed, GetLastError=" << GetLastError() << L"\n";
        return false;
    }

    if (opt.write_test_data) {
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(h, &size)) {
            std::wcerr << L"GetFileSizeEx failed, GetLastError=" << GetLastError() << L"\n";
            CloseHandle(h);
            return false;
        }

        if (size.QuadPart == 0) {
            const char kData[] = "FileLockSimulator test content.\r\n";
            DWORD written = 0;
            if (!WriteFile(h, kData, static_cast<DWORD>(sizeof(kData) - 1), &written, nullptr)) {
                std::wcerr << L"WriteFile failed, GetLastError=" << GetLastError() << L"\n";
                CloseHandle(h);
                return false;
            }
            FlushFileBuffers(h);
        }
    }

    CloseHandle(h);
    return true;
}

static uint64_t GetFileLength(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        return 0;
    }

    ULARGE_INTEGER li{};
    li.LowPart = fad.nFileSizeLow;
    li.HighPart = fad.nFileSizeHigh;
    return li.QuadPart;
}

static HANDLE OpenForHold(const Options& opt) {
    DWORD shareMode = BuildShareMode(opt.share);

    HANDLE h = CreateFileW(
        opt.path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        shareMode,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (h == INVALID_HANDLE_VALUE) {
        std::wcerr << L"CreateFileW failed, GetLastError=" << GetLastError() << L"\n";
        return INVALID_HANDLE_VALUE;
    }

    return h;
}

static bool LockRange(HANDLE h, uint64_t offset, uint64_t length) {
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFULL);
    ov.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFFULL);

    DWORD lenLow = static_cast<DWORD>(length & 0xFFFFFFFFULL);
    DWORD lenHigh = static_cast<DWORD>((length >> 32) & 0xFFFFFFFFULL);

    if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, lenLow, lenHigh, &ov)) {
        std::wcerr << L"LockFileEx failed, GetLastError=" << GetLastError() << L"\n";
        return false;
    }

    return true;
}

static bool UnlockRange(HANDLE h, uint64_t offset, uint64_t length) {
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFULL);
    ov.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFFULL);

    DWORD lenLow = static_cast<DWORD>(length & 0xFFFFFFFFULL);
    DWORD lenHigh = static_cast<DWORD>((length >> 32) & 0xFFFFFFFFULL);

    if (!UnlockFileEx(h, 0, lenLow, lenHigh, &ov)) {
        std::wcerr << L"UnlockFileEx failed, GetLastError=" << GetLastError() << L"\n";
        return false;
    }

    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    Options opt;
    if (!ParseArgs(argc, argv, opt)) {
        PrintUsage();
        return 1;
    }

    if (!EnsureFileExists(opt)) {
        return 2;
    }

    HANDLE h = OpenForHold(opt);
    if (h == INVALID_HANDLE_VALUE) {
        return 3;
    }

    uint64_t fileLen = GetFileLength(opt.path);
    uint64_t lockLen = opt.length;

    if (lockLen == 0) {
        if (fileLen > opt.offset) {
            lockLen = fileLen - opt.offset;
        } else {
            lockLen = 1;
        }
    }

    bool locked = false;

    std::wcout << L"========================================\n";
    std::wcout << L"FileLockSimulator started\n";
    std::wcout << L"PID        : " << GetCurrentProcessId() << L"\n";
    std::wcout << L"Mode       : " << opt.mode << L"\n";
    std::wcout << L"Path       : " << opt.path << L"\n";
    std::wcout << L"Share      : " << opt.share << L"\n";
    std::wcout << L"Offset     : " << opt.offset << L"\n";
    std::wcout << L"Length     : " << lockLen << L"\n";
    std::wcout << L"File size  : " << fileLen << L"\n";
    std::wcout << L"========================================\n";

    if (opt.mode == L"lock" || opt.mode == L"both") {
        if (!LockRange(h, opt.offset, lockLen)) {
            CloseHandle(h);
            return 4;
        }
        locked = true;
        std::wcout << L"[OK] Byte-range lock acquired.\n";
    }

    if (opt.mode == L"open" || opt.mode == L"both") {
        std::wcout << L"[OK] File handle is being held open.\n";
    }

    std::wcout << L"\nThe file is now in simulated test state.\n";
    std::wcout << L"Press ENTER to release and exit...\n";
    std::wstring dummy;
    std::getline(std::wcin, dummy);

    if (locked) {
        UnlockRange(h, opt.offset, lockLen);
    }

    CloseHandle(h);
    std::wcout << L"Released. Exit.\n";
    return 0;
}