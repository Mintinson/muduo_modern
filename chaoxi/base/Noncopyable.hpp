#ifndef MUDUO_BASE_NONCOPYABLE_HPP
#define MUDUO_BASE_NONCOPYABLE_HPP

namespace chaoxi
{
class NonCopyable
{
public:
    NonCopyable(const NonCopyable&) = delete;
    void operator=(const NonCopyable&) = delete;

protected:
    NonCopyable() = default;
    ~NonCopyable() = default;
};
}  // namespace chaoxi

#endif  // MUDUO_BASE_NONCOPYABLE_HPP
