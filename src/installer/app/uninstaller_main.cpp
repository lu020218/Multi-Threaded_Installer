#include "installer/app/launch_support.h"

using namespace MultiThreadedInstaller;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    const LaunchContext context = BuildLaunchContextFromCommandLine(LaunchBinary::Uninstaller);
    return RunLaunchContext(hInstance, context);
}
