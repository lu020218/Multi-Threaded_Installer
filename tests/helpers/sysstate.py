# -*- coding: utf-8 -*-
"""系统状态断言与清扫：产品注册表 / ARP 卸载入口 / 自启 / 桌面快捷方式 / 全树 md5。
所有写/删操作对产品名做前缀防护，只允许作用于 MtiAutoTest* 产品。"""
import hashlib
import os
import pathlib
import winreg

from . import env

_UNINSTALL_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall"
_RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"


def _guard(product: str):
    if not product.startswith(env.PRODUCT_PREFIX):
        raise RuntimeError(
            "拒绝对非测试产品执行状态操作: %r（必须以 %s 开头）"
            % (product, env.PRODUCT_PREFIX))


def _read_value(hive, subkey, name, view=winreg.KEY_WOW64_64KEY):
    try:
        with winreg.OpenKey(hive, subkey, 0, winreg.KEY_READ | view) as k:
            return winreg.QueryValueEx(k, name)[0]
    except OSError:
        return None


def product_reg(product: str) -> dict:
    """读 HKLM\\Software\\<product> 的常用值（不存在返回空 dict）。"""
    out = {}
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"Software\%s" % product,
                            0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as k:
            for name in ("Version", "InstallDir", "InstallState", "Language"):
                try:
                    out[name] = winreg.QueryValueEx(k, name)[0]
                except OSError:
                    pass
    except OSError:
        pass
    return out


def arp_entry(product: str) -> dict:
    out = {}
    sub = _UNINSTALL_KEY + "\\" + product
    for name in ("DisplayName", "DisplayVersion", "UninstallString", "InstallLocation"):
        v = _read_value(winreg.HKEY_LOCAL_MACHINE, sub, name)
        if v is not None:
            out[name] = v
    return out


def autostart_value(product: str):
    return _read_value(winreg.HKEY_CURRENT_USER, _RUN_KEY, product, view=0)


def _real_desktop() -> pathlib.Path:
    """真实桌面路径（与安装器 SHGetFolderPath(CSIDL_DESKTOPDIRECTORY) 一致，支持重定向）。"""
    import ctypes
    from ctypes import wintypes
    CSIDL_DESKTOPDIRECTORY = 0x0010
    SHGFP_TYPE_CURRENT = 0
    buf = ctypes.create_unicode_buffer(wintypes.MAX_PATH)
    ctypes.windll.shell32.SHGetFolderPathW(None, CSIDL_DESKTOPDIRECTORY, None,
                                           SHGFP_TYPE_CURRENT, buf)
    return pathlib.Path(buf.value)


def desktop_shortcut(product: str) -> pathlib.Path:
    return _real_desktop() / (product + ".lnk")


def residue(product: str) -> list:
    """返回残留项名称列表（空=干净）。"""
    _guard(product)
    out = []
    if product_reg(product):
        out.append("product-reg")
    if arp_entry(product):
        out.append("arp")
    if autostart_value(product) is not None:
        out.append("autostart")
    if desktop_shortcut(product).exists():
        out.append("desktop-shortcut")
    return out


def force_cleanup(product: str, install_dirs=None):
    """强制清扫某测试产品的一切系统状态（用例结束/失败兜底）。仅限 MtiAutoTest* 产品。"""
    _guard(product)
    # 注册表
    for hive, sub, view in (
        (winreg.HKEY_LOCAL_MACHINE, r"Software\%s" % product, winreg.KEY_WOW64_64KEY),
        (winreg.HKEY_LOCAL_MACHINE, _UNINSTALL_KEY + "\\" + product, winreg.KEY_WOW64_64KEY),
    ):
        try:
            winreg.DeleteKeyEx(hive, sub, view, 0)
        except OSError:
            pass
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, _RUN_KEY, 0, winreg.KEY_SET_VALUE) as k:
            winreg.DeleteValue(k, product)
    except OSError:
        pass
    # 桌面快捷方式
    lnk = desktop_shortcut(product)
    try:
        lnk.unlink()
    except OSError:
        pass
    # install-state.json 目录
    programdata = pathlib.Path(os.environ.get("ProgramData", r"C:\ProgramData"))
    _rmtree(programdata / product)
    # 安装目录
    for d in (install_dirs or []):
        _rmtree(pathlib.Path(d))


def _rmtree(path: pathlib.Path):
    import shutil
    try:
        if path.exists():
            shutil.rmtree(path, ignore_errors=True)
    except OSError:
        pass


def tree_mismatches(src_app: pathlib.Path, dst: pathlib.Path) -> list:
    """逐文件 md5 对比 src_app 与 dst，返回 (缺失|差异) 相对路径列表。"""
    bad = []
    for root, _, files in os.walk(src_app):
        for f in files:
            s = pathlib.Path(root) / f
            rel = s.relative_to(src_app)
            d = dst / rel
            if not d.exists():
                bad.append("MISSING:" + str(rel))
            elif hashlib.md5(s.read_bytes()).digest() != hashlib.md5(d.read_bytes()).digest():
                bad.append("DIFF:" + str(rel))
    return bad
