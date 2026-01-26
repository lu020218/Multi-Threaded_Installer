#pragma once

#include <cstdint>
#include <cstddef>

namespace MultiThreadedInstaller {

struct StreamSink {
    virtual ~StreamSink() = default;
    virtual bool write(const uint8_t* data, size_t size) = 0;
    virtual void flush() = 0;
};

} // namespace MultiThreadedInstaller
