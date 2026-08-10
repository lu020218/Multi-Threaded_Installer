# -*- coding: utf-8 -*-
"""GUI 驱动：Tier A（--upgrade 自启零点击）+ Tier B（后门脚本 MTI_AUTOTEST_SCRIPT）。
关闭窗口用 ctypes 枚举该进程的顶层窗口并发 WM_CLOSE，不引入外部依赖。"""
import ctypes
import os
import subprocess
import time
from ctypes import wintypes

_user32 = ctypes.windll.user32
_WM_CLOSE = 0x0010

_EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def _close_windows_of_pid(pid: int):
    def _cb(hwnd, _lparam):
        wpid = wintypes.DWORD()
        _user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid and _user32.IsWindowVisible(hwnd):
            _user32.PostMessageW(hwnd, _WM_CLOSE, 0, 0)
        return True
    _user32.EnumWindows(_EnumProc(_cb), 0)


def launch_gui(setup_exe, *, script_path=None, browse_result=None, upgrade=False,
               extra_args=None, wait_secs=14):
    """启动 GUI 安装器（后门/自启模式）。等其自然退出或到点后关窗。返回退出码或 None。"""
    env = os.environ.copy()
    if script_path:
        env["MTI_AUTOTEST_SCRIPT"] = str(script_path)
    if browse_result:
        env["MTI_AUTOTEST_BROWSE_RESULT"] = str(browse_result)
    args = [str(setup_exe)]
    if upgrade:
        args.append("--upgrade")
    if extra_args:
        args += extra_args
    proc = subprocess.Popen(args, env=env)
    deadline = time.time() + wait_secs
    while time.time() < deadline and proc.poll() is None:
        time.sleep(0.5)
    if proc.poll() is None:
        _close_windows_of_pid(proc.pid)
        time.sleep(2)
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=10)
    return proc.poll()


def write_script(path, steps):
    path.write_text("\r\n".join(steps) + "\r\n", encoding="utf-8", newline="")
