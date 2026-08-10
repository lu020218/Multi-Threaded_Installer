// Fake component installer for tests: parse --sleep seconds and --rc exit code.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>
#include <cstring>

int wmain(int argc, wchar_t** argv) {
    int sleepSec = 0;
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--sleep") == 0 && i + 1 < argc) {
            sleepSec = _wtoi(argv[++i]);
        } else if (wcscmp(argv[i], L"--rc") == 0 && i + 1 < argc) {
            rc = _wtoi(argv[++i]);
        }
    }
    if (sleepSec > 0) {
        ::Sleep(static_cast<DWORD>(sleepSec) * 1000u);
    }
    return rc;
}
