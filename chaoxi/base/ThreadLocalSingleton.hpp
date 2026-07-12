// #pragma once

// #include <memory>
// #include <type_traits>

// namespace chaoxi
// {

// template <typename T>
// class ThreadLocalSingleton final
// {
// public:
//     ThreadLocalSingleton() = delete;
//     ~ThreadLocalSingleton() = delete;

//     ThreadLocalSingleton(const ThreadLocalSingleton&) = delete;
//     ThreadLocalSingleton& operator=(const ThreadLocalSingleton&) = delete;
//     ThreadLocalSingleton(ThreadLocalSingleton&&) = delete;
//     ThreadLocalSingleton& operator=(ThreadLocalSingleton&&) = delete;

//     [[nodiscard]]
//     static T& instance() noexcept(std::is_nothrow_default_constructible_v<T>)
//     {
//         return value();
//     }

//     [[nodiscard]]
//     static T* pointer() noexcept
//     {
//         return initialized_ ? std::addressof(value_) : nullptr;
//     }

// private:
//     static T& value() noexcept(std::is_nothrow_default_constructible_v<T>)
//     {
//         initialized_ = true;
//         return value_;
//     }

//     inline static thread_local T value_{};
//     inline static thread_local bool initialized_ = false;
// };

// }  // namespace chaoxi

// #pragma once

// #include <concepts>
// #include <memory>
// #include <stdexcept>
// #include <utility>

// namespace chaoxi
// {

// template <typename T>
// class ThreadLocalSingleton final
// {
// public:
//     ThreadLocalSingleton() = delete;
//     ~ThreadLocalSingleton() = delete;

//     ThreadLocalSingleton(const ThreadLocalSingleton&) = delete;
//     ThreadLocalSingleton& operator=(const ThreadLocalSingleton&) = delete;
//     ThreadLocalSingleton(ThreadLocalSingleton&&) = delete;
//     ThreadLocalSingleton& operator=(ThreadLocalSingleton&&) = delete;

//     [[nodiscard]]
//     static T& instance()
//         requires std::default_initializable<T>
//     {
//         if (!value_)
//         {
//             value_ = std::make_unique<T>();
//         }

//         return *value_;
//     }

//     template <typename... Args>
//         requires std::constructible_from<T, Args...>
//     [[nodiscard]]
//     static T& emplace(Args&&... args)
//     {
//         if (value_)
//         {
//             throw std::logic_error("ThreadLocalSingleton is already
//             initialized "
//                                    "in the current thread");
//         }

//         value_ = std::make_unique<T>(std::forward<Args>(args)...);

//         return *value_;
//     }

//     [[nodiscard]]
//     static T* pointer() noexcept
//     {
//         return value_.get();
//     }

//     [[nodiscard]]
//     static bool initialized() noexcept
//     {
//         return static_cast<bool>(value_);
//     }

//     static void reset() noexcept { value_.reset(); }

// private:
//     inline static thread_local std::unique_ptr<T> value_;
// };

// }  // namespace chaoxi

#pragma once

#include <memory>
#include <type_traits>

namespace chaoxi
{

template <typename T>
    requires std::is_default_constructible_v<T>
class ThreadLocalSingleton final
{
public:
    ThreadLocalSingleton() = delete;
    ~ThreadLocalSingleton() = delete;

    [[nodiscard]]
    static T& instance() noexcept(std::is_nothrow_default_constructible_v<T>)
    {
        static thread_local T value;
        return value;
    }

    [[nodiscard]]
    static T* pointer() noexcept(std::is_nothrow_default_constructible_v<T>)
    {
        return std::addressof(instance());
    }
};

}  // namespace chaoxi