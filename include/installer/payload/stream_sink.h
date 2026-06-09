#pragma once

#include <cstdint>
#include <cstddef>

namespace MultiThreadedInstaller {

/// 解压输出的抽象接收端：解压器把还原出的字节流喂给具体 sink（写文件 / 校验 / tar 解析等），
/// 从而把"解压"与"落地方式"解耦。
struct StreamSink {
    virtual ~StreamSink() = default;
    /// 写入一段还原出的数据；返回 false 表示写失败（上层应中止）。
    virtual bool write(const uint8_t* data, size_t size) = 0;
    /// 刷新缓冲，确保数据落地。
    virtual void flush() = 0;
};

} // namespace MultiThreadedInstaller
