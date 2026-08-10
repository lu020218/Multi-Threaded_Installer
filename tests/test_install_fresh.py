# -*- coding: utf-8 -*-
"""全新安装：全树 md5、双 folder、manifest app 对象、注册表/ARP/自启。
桌面快捷方式依赖交互式桌面（IShellLink::Save），headless/服务会话下不可用，
单列 test_shortcuts 弹性验证。"""
import json

import pytest

from helpers import corpus, installer, logparse, sysstate


@pytest.mark.fresh
def test_fresh_install_full(make_package, case_dir, product):
    app_root = case_dir / "input"
    app = corpus.make_payload(app_root, dirs=6, files_per_dir=40,
                              big_files={"big.bin": 6 * 1024 * 1024}, chinese=True)
    (app / "tsmain.exe").write_bytes(b"MZ" + b"\x00" * 4096)  # 主 exe（自启/快捷方式定位用）
    plugins = corpus.make_plugins(app_root)
    (plugins / "readme.txt").write_text("plugin payload\n", encoding="utf-8")
    setup, res = make_package(
        app_root, with_icon=True,
        layout=[("app", "%InstallDir%"), ("plugins", "%InstallDir%\\plugins")])
    assert res.returncode == 0, res.stdout + res.stderr

    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))
    rc, _ = installer.silent_install(setup, dst, auto_startup=True, desktop_icon=True)
    assert rc == 0

    # 全树逐文件 md5（含中文路径、大文件独帧、双 folder 落点）
    assert sysstate.tree_mismatches(app, dst) == []
    assert (dst / "plugins" / "readme.txt").exists()  # 第二个 folder 落点
    assert (dst / "中文目录" / "配置文件.txt").exists()  # 中文路径

    # manifest app 对象与 yaml 字段一一对应
    manifest = json.loads((dst / "install.manifest.json").read_text(encoding="utf-8"))
    assert manifest["app"] == {
        "productName": product["name"], "appName": "tsmain",
        "appId": "com.test.auto", "version": "1.0.0", "publisher": "AutoTest"}

    # 产品注册表 / ARP / 自启（均不依赖交互桌面）
    reg = sysstate.product_reg(product["name"])
    assert reg.get("Version") == "1.0.0"
    assert reg.get("InstallDir", "").rstrip("\\").lower() == str(dst).rstrip("\\").lower()
    arp = sysstate.arp_entry(product["name"])
    assert arp.get("DisplayName") == product["name"]
    assert arp.get("DisplayVersion") == "1.0.0"
    assert sysstate.autostart_value(product["name"]) is not None


@pytest.mark.fresh
def test_shortcuts(make_package, case_dir, product):
    """桌面 + 开始菜单快捷方式。需交互式桌面；服务/headless 会话下 IShellLink::Save
    失败，此时按环境限制跳过（非产品缺陷）。"""
    app_root = case_dir / "input"
    corpus.make_payload(app_root, dirs=1, files_per_dir=2)
    (app_root / "app" / "tsmain.exe").write_bytes(b"MZ" + b"\x00" * 512)
    setup, res = make_package(app_root)
    assert res.returncode == 0

    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))
    rc, _ = installer.silent_install(setup, dst, desktop_icon=True)
    assert rc == 0

    log = logparse.read_latest(product["name"])
    if "Failed to create desktop icon" in log:
        pytest.skip("当前为非交互式桌面会话，IShellLink::Save 不可用（环境限制）")
    assert "Desktop icon created" in log
    assert sysstate.desktop_shortcut(product["name"]).exists()
