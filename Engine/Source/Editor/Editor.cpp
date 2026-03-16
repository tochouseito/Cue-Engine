#include <iostream>
#define CUE_PLATFORM_WINDOWS
#include <CueAssert.h>

int main()
{
    std::cout << "Hello, Editor!" << std::endl;
    CUE_ASSERTF(false, "Test assertion failed %d", 42);
    return 0;
}
