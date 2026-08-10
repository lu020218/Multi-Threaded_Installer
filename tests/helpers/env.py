# -*- coding: utf-8 -*-
"""路径/二进制/管理员等运行环境探测。全部围绕仓库根 build/Release 定位产物。"""
import ctypes
import os
import pathlib

# tests/helpers/env.py -> 仓库根
REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
RELEASE_DIR = REPO_ROOT / "build" / "Release"

PACKAGER_EXE = RELEASE_DIR / "packager.exe"
INSTALLER_EXE = RELEASE_DIR / "installer.exe"
UNINSTALLER_EXE = RELEASE_DIR / "uninstaller.exe"
FILELOCK_EXE = REPO_ROOT / "build" / "test" / "Release" / "FileLockSimulator.exe"
RESOURCES_DIR = REPO_ROOT / "resources"
APP_ICON = RELEASE_DIR / "config" / "app.ico"

# 安装器日志目录（引擎写死落点）。
INSTALLER_LOG_DIR = pathlib.Path(os.environ["LOCALAPPDATA"]) / "MTInstaller"

# 所有测试产品名统一前缀——清理防护只允许删除该前缀下的系统状态，杜绝误删真实产品。
PRODUCT_PREFIX = "MtiAutoTest"


def is_admin() -> bool:
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def require_binaries() -> list:
    """返回缺失的必需二进制列表（空=齐全）。"""
    missing = []
    for exe in (PACKAGER_EXE, INSTALLER_EXE, UNINSTALLER_EXE):
        if not exe.exists():
            missing.append(str(exe))
    return missing
