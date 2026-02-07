#include <Windows.h>
#include <iostream>

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    std::cout << "Hello CMake from App." << std::endl;
    return 0;
}
