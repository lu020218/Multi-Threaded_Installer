# -*- coding: utf-8 -*-
"""GUI 自动化：Tier A（零点击全链路/单实例）+ Tier B（后门驱动欢迎页交互）。"""
import time

import pytest

from helpers import corpus, gui, logparse, sysstate, env


def _pkg(make_package, case_dir):
    app_root = case_dir / "input"
    corpus.make_payload(app_root, dirs=1, files_per_dir=3)
    (app_root / "app" / "tsmain.exe").write_bytes(b"MZ" + b"\x00" * 512)
    setup, res = make_package(app_root, with_icon=True)
    assert res.returncode == 0, res.stdout + res.stderr
    return setup


@pytest.mark.gui
def test_gui_backdoor_install_to_custom_dir(make_package, case_dir, product):
    """Tier B：脚本 勾选许可→改路径→点安装，实际装入自定义目录。"""
    setup = _pkg(make_package, case_dir)
    dst = case_dir / "gdst"
    product["install_dirs"].append(str(dst))
    script = case_dir / "s.txt"
    gui.write_script(script, [
        "wait 800",
        "setcheck license_checkbox 1",
        "wait 300",
        "settext editDir %s" % dst,
        "wait 300",
        "click btnInstall",
    ])
    gui.launch_gui(setup, script_path=script)
    assert (dst / "install.manifest.json").exists(), "GUI 未装入自定义目录"
    assert sysstate.product_reg(product["name"]).get("InstallDir", "").rstrip("\\").lower() \
        == str(dst).rstrip("\\").lower()


@pytest.mark.gui
def test_gui_browse_injection_appends_product_dir(make_package, case_dir, product):
    """Tier B + 浏览注入：点浏览→注入路径→ResolveSelectedInstallPath 追加产品目录名。"""
    setup = _pkg(make_package, case_dir)
    browsed = case_dir / "browsed"
    expect = browsed / product["name"]     # 浏览目录下追加产品名
    product["install_dirs"].append(str(expect))
    script = case_dir / "s.txt"
    gui.write_script(script, [
        "wait 800",
        "setcheck license_checkbox 1",
        "wait 300",
        "click btnSelectDir",
        "wait 400",
        "click btnInstall",
    ])
    gui.launch_gui(setup, script_path=script, browse_result=browsed)
    assert (expect / "install.manifest.json").exists(), "浏览注入未装入 <browsed>/<product>"


@pytest.mark.gui
def test_gui_upgrade_autostart_zero_click(make_package, case_dir, product):
    """Tier A：先静默装 v1，再 GUI --upgrade 零点击自动升级 v2，走真实 GUI 管线。"""
    from helpers import installer
    app_root = case_dir / "input"
    app = corpus.make_payload(app_root, dirs=2, files_per_dir=10)
    (app_root / "app" / "tsmain.exe").write_bytes(b"MZ" + b"\x00" * 512)
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))

    s1, _ = make_package(app_root, version="1.0.0", output_name="v1.exe")
    assert installer.silent_install(s1, dst)[0] == 0

    s2, _ = make_package(app_root, version="2.0.0", with_icon=True, output_name="v2.exe")
    gui.launch_gui(s2, upgrade=True)
    time.sleep(1)
    # 升级后注册表版本应为 2.0.0，安装目录沿用
    assert sysstate.product_reg(product["name"]).get("Version") == "2.0.0"


@pytest.mark.gui
def test_gui_single_instance(make_package, case_dir, product):
    """Tier A：同时启两个 GUI，第二个应被单实例互斥挡下（仅一个存活）。"""
    import subprocess
    setup = _pkg(make_package, case_dir)
    p1 = subprocess.Popen([str(setup)])
    time.sleep(3)
    p2 = subprocess.Popen([str(setup)])
    time.sleep(3)
    alive = sum(1 for p in (p1, p2) if p.poll() is None)
    for p in (p1, p2):
        gui._close_windows_of_pid(p.pid)
    time.sleep(2)
    for p in (p1, p2):
        if p.poll() is None:
            p.kill()
        p.wait(timeout=10)
    assert alive == 1, "单实例互斥失效，存活=%d" % alive
