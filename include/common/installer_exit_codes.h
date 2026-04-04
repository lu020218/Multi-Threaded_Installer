#pragma once

namespace MultiThreadedInstaller {

enum InstallerExitCode {
    INSTALLER_EXIT_SUCCESS = 0,
    INSTALLER_EXIT_FAILED = 1,
    INSTALLER_EXIT_CANCELLED = 2,
    INSTALLER_EXIT_ADMIN_REQUIRED = 3,
    INSTALLER_EXIT_REBOOT_REQUIRED = 4
};

} // namespace MultiThreadedInstaller
