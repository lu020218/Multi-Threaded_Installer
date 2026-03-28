#include "installer/launch_support.h"

using namespace MultiThreadedInstaller;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    const LaunchContext context = BuildLaunchContextFromCommandLine(LaunchBinary::Installer);
    return RunLaunchContext(hInstance, context);
}
