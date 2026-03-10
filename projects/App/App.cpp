#include <Windows.h>
#include <iostream>

// windows アプリのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    std::cout << "Hello CMake from App." << std::endl;
    return 0;
}
