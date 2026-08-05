#include "codec.hpp"

#include <string_view>

namespace pubsub
{
ParseResult parseMessage(chaoxi::net::Buffer& buf,
                         std::string& cmd,
                         std::string& topic,
                         std::string& content)
{
    // peek() 只观察 Buffer，不移动读指针。解析完成前始终保留 begin，最后再
    // 一次性 retrieveUntil()，这是处理 TCP 半包时最关键的约束。
    const char* const begin = buf.peek();
    const char* const headerEnd = buf.findCRLF();

    // 第一行还没收完整，例如当前只有 "pub news\r"。
    if (headerEnd == nullptr)
    {
        return ParseResult::kContinue;
    }

    const std::string_view header{begin,
                                  static_cast<std::size_t>(headerEnd - begin)};

    // 首个空格把命令和主题分开。主题本身不允许包含空格，这使协议解析无需
    // 转义规则，适合作为网络编程示例。
    const std::size_t separator = header.find(' ');

    // 同时拒绝：
    // 1. 没有空格
    // 2. 空命令，例如 " topic"
    // 3. 空主题，例如 "pub "
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 == header.size())
    {
        return ParseResult::kError;
    }

    const std::string_view commandView = header.substr(0, separator);
    const std::string_view topicView = header.substr(separator + 1);

    const char* frameEnd = headerEnd + 2;
    std::string_view contentView;

    // sub/unsub 只有一行；pub 还有第二行内容，因此必须继续寻找第二个 CRLF。
    if (commandView == "pub")
    {
        const char* const contentBegin = frameEnd;
        const char* const contentEnd = buf.findCRLF(contentBegin);

        if (contentEnd == nullptr)
        {
            return ParseResult::kContinue;
        }

        contentView = std::string_view{
            contentBegin, static_cast<std::size_t>(contentEnd - contentBegin)};

        frameEnd = contentEnd + 2;
    }

    // 到这里说明整帧已经完整且头部合法，现在才“提交”解析结果。因此
    // kContinue 和 kError 都不会污染输出参数。
    cmd.assign(commandView);
    topic.assign(topicView);

    if (commandView == "pub")
    {
        content.assign(contentView);
    }
    else
    {
        content.clear();
    }

    // string_view 指向 Buffer 内部存储；必须在所有 view 使用完之后才能移动
    // Buffer 的读指针，否则后续 Buffer 操作可能使这些视图失效。
    buf.retrieveUntil(frameEnd);

    return ParseResult::kSuccess;
}
}  // namespace pubsub
