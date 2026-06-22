#include <cstddef>
#include <iostream>
#include <print>

int main()
{
    std::println("{}, {}", sizeof(int), sizeof(std::size_t));
}