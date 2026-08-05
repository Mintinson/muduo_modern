#pragma once

#include "chaoxi/net/Buffer.hpp"

#include <cstdint>
#include <string>

namespace pubsub
{

// hub 示例使用一个刻意保持简单的文本协议。每条命令由 CRLF 分隔：
//
//   sub <topic>\r\n
//   unsub <topic>\r\n
//   pub <topic>\r\n<content>\r\n
//
// TCP 是字节流，没有“消息边界”。一次 read 可能只得到半条命令，也可能同时
// 得到多条命令，因此解析器必须能够报告“数据尚未收全”，并把未消费的数据留
// 在 Buffer 中等待下一次 read。
enum class ParseResult : std::uint8_t
{
    // 当前缓冲区已经包含一条完整但格式非法的消息。
    kError,

    // 成功解析并消费了一条完整消息，调用者可以继续解析下一条。
    kSuccess,

    // 当前消息还不完整。解析器不消费 Buffer，也不修改输出参数。
    kContinue,
};

/// 从 buf 的可读区域解析一条消息。
///
/// 这是一个“提交式”解析器：只有返回 kSuccess 时才会更新 cmd/topic/content
/// 并移动 Buffer 的读指针。这样，半包到达时不会丢失数据，调用者也不会观察
/// 到只填了一半的输出结果。
ParseResult parseMessage(chaoxi::net::Buffer& buf,
                         std::string& cmd,
                         std::string& topic,
                         std::string& content);
}  // namespace pubsub
