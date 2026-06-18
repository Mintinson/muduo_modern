#include <iostream>
#include <source_location>

int main()
{
    std::source_location loc = std::source_location::current();

    std::cout << "sizeof std::source_location: " << sizeof(std::source_location) << " bytes\n";
    std::cout << "File: " << loc.file_name() << "\n";
}