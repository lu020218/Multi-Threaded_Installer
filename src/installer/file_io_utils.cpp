#include "installer/installer_helpers.h"

#include "common/utf8_utils.h"

#ifdef _WIN32
#include <Windows.h>
#include <winioctl.h>
#endif

#include <fstream>
#include <vector>

namespace MultiThreadedInstaller {

std::filesystem::path toLongPath(const std::filesystem::path& path) {
#ifdef _WIN32
    std::wstring native = path.native();
    if (native.rfind(LR"(\\?\)", 0) == 0) {
        return path;
    }
    if (native.rfind(LR"(\\)", 0) == 0) {
        std::wstring unc = LR"(\\?\UNC\)" + native.substr(2);
        return std::filesystem::path(unc);
    }
    if (!path.is_absolute()) {
        native = std::filesystem::absolute(path).native();
    }
    std::wstring longPath = LR"(\\?\)" + native;
    return std::filesystem::path(longPath);
#else
    return path;
#endif
}

bool ensureFileWithSize(const std::filesystem::path& path,
                        uint64_t size,
                        uint64_t sparseThresholdBytes) {
#ifdef _WIN32
    std::filesystem::path openPath = toLongPath(path);
    HANDLE handle = CreateFileW(openPath.c_str(),
                                GENERIC_WRITE,
                                FILE_SHARE_READ,
                                nullptr,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytesReturned = 0;
    if (size >= sparseThresholdBytes) {
        (void)DeviceIoControl(
            handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
    }

    if (size > 0) {
        LARGE_INTEGER newSize;
        newSize.QuadPart = static_cast<LONGLONG>(size);
        if (!SetFilePointerEx(handle, newSize, nullptr, FILE_BEGIN) ||
            !SetEndOfFile(handle)) {
            CloseHandle(handle);
            return false;
        }
    }

    CloseHandle(handle);
    return true;
#else
    std::filesystem::path openPath = toLongPath(path);
    std::fstream file(openPath,
                      std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!file) {
        std::ofstream create(openPath, std::ios::binary | std::ios::trunc);
        if (!create) {
            return false;
        }
        create.close();
        file.open(openPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!file) {
            return false;
        }
    }

    if (size > 0) {
        file.seekp(static_cast<std::streamoff>(size - 1));
        char zero = 0;
        file.write(&zero, 1);
        file.flush();
    }

    return static_cast<bool>(file);
#endif
}

bool openFileForWrite(const std::filesystem::path& path, std::fstream& stream) {
    std::filesystem::path openPath = toLongPath(path);
    stream.open(openPath, std::ios::binary | std::ios::in | std::ios::out);
    if (!stream) {
        std::ofstream create(openPath, std::ios::binary | std::ios::app);
        if (!create) {
            return false;
        }
        create.close();
        stream.open(openPath, std::ios::binary | std::ios::in | std::ios::out);
    }
    return static_cast<bool>(stream);
}

bool createUninstallStub(const std::string& sourcePath, const std::string& targetPath) {
    std::ifstream in(toLongPath(PathFromUtf8(sourcePath)), std::ios::binary);
    if (!in) {
        return false;
    }

    in.seekg(0, std::ios::end);
    uint64_t fileSize = static_cast<uint64_t>(in.tellg());
    uint64_t trailerEnd = 0;
    EmbeddedDataLocatorRecord locator{};
    if (!findEmbeddedDataLocator(in, fileSize, trailerEnd, locator) ||
        locator.metadataOffset == 0) {
        return false;
    }

    if (locator.metadataOffset >= trailerEnd) {
        return false;
    }

    std::ofstream out(toLongPath(PathFromUtf8(targetPath)),
                      std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    in.seekg(0, std::ios::beg);
    const size_t bufSize = 1024 * 1024;
    std::vector<char> buffer(bufSize);
    uint64_t remaining = locator.metadataOffset;
    while (remaining > 0) {
        size_t chunk = remaining > bufSize ? bufSize : static_cast<size_t>(remaining);
        in.read(buffer.data(), static_cast<std::streamsize>(chunk));
        if (!in) {
            return false;
        }
        out.write(buffer.data(), static_cast<std::streamsize>(chunk));
        if (!out) {
            return false;
        }
        remaining -= chunk;
    }
    return true;
}

}  // namespace MultiThreadedInstaller
