#pragma once

namespace MultiThreadedInstaller {

/// installer.exe / uninstaller.exe 的进程退出码（供脚本/上层判断结果）。
enum InstallerExitCode {
    INSTALLER_EXIT_SUCCESS = 0,          ///< 成功。
    INSTALLER_EXIT_FAILED = 1,           ///< 失败（含校验失败、解压失败、hook abort 等）。
    INSTALLER_EXIT_CANCELLED = 2,        ///< 被用户取消。
    INSTALLER_EXIT_ADMIN_REQUIRED = 3,   ///< 需要管理员权限但提权失败/被拒。
    INSTALLER_EXIT_REBOOT_REQUIRED = 4,  ///< 安装完成但有锁定文件待重启替换。
    INSTALLER_EXIT_ALREADY_RUNNING = 5   ///< 已有同类实例在运行，本次启动被单例拦截而跳过（未安装/卸载）。
};

} // namespace MultiThreadedInstaller
