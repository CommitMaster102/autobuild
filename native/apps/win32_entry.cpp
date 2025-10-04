#ifdef _WIN32
#include <windows.h>

// Forward declare the real main provided
extern "C" int main(int argc, char** argv);

int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR     lpCmdLine,
                     int       nCmdShow) {
    // Delegate to standard main to keep all logic in one place
    return main(__argc, __argv);
}
#endif


