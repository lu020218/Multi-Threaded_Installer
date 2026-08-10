# -*- coding: utf-8 -*-
"""安装/升级/卸载调用封装 + CLI 进度帧捕获。"""
import subprocess
import time

from . import env


def silent_install(setup_exe, destination, *, upgrade=False, language=None,
                   auto_startup=None, desktop_icon=None, components=None,
                   timeout=600):
    """静默安装/升级。返回 (returncode, progress_frames)。
    progress_frames：stdout 按 \\r/\\n 切分后以 'Installing:' 开头的行（进度契约测试用）。"""
    args = [str(setup_exe), "--silent"]
    if destination is not None:
        args += ["--destination", str(destination)]
    if upgrade:
        args.append("--upgrade")
    if language is not None:
        args += ["--language", language]
    if auto_startup is not None:
        args += ["--auto-startup", "1" if auto_startup else "0"]
    if desktop_icon is not None:
        args += ["--desktop-icon", "1" if desktop_icon else "0"]
    if components is not None:
        args += ["--components", components]
    proc = subprocess.run(args, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=timeout)
    frames = []
    for chunk in (proc.stdout or "").replace("\r", "\n").split("\n"):
        chunk = chunk.strip()
        if chunk.startswith("Installing:"):
            frames.append(chunk)
    return proc.returncode, frames


def silent_uninstall(uninstall_exe, *, timeout=300):
    proc = subprocess.run([str(uninstall_exe), "--silent"],
                          capture_output=True, text=True, timeout=timeout)
    time.sleep(1.5)  # 让 pending 删除/自删收尾
    return proc.returncode


def progress_percents(frames):
    """从进度帧里抽取百分比整数序列。"""
    import re
    out = []
    for f in frames:
        m = re.search(r"(\d+)%", f)
        if m:
            out.append(int(m.group(1)))
    return out
