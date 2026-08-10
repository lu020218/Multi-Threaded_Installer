# -*- coding: utf-8 -*-
"""钩子脚本：pre/post 执行、兄弟脚本、keep 保留、onFailure=abort 回滚/continue。"""
import pytest

from helpers import corpus, installer, logparse, sysstate

_BS = "\\"


def _hooks_yaml(*, pre=None, post=None):
    lines = ["hooks:"]
    if pre:
        lines.append("  preInstall:")
        for entry in pre:
            lines.append("    - " + entry)
    if post:
        lines.append("  postInstall:")
        for entry in post:
            lines.append("    - " + entry)
    return "\r\n".join(lines) + "\r\n"


def _write_script(config_dir, rel, body_lines):
    p = config_dir / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(("\r\n".join(body_lines) + "\r\n").encode("ascii"))


@pytest.mark.hooks
def test_pre_post_sibling_keep(make_package, case_dir, product):
    app_root = case_dir / "input"
    corpus.make_payload(app_root, dirs=1, files_per_dir=3)
    # 先建 config，写脚本，再打包（make_package 内部 prepare_config 会保留已存在文件）
    from helpers import packager
    cfg = case_dir / "config"
    packager.prepare_config(cfg)
    _write_script(cfg, "scripts/pre.bat",
                  ["@echo off",
                   'if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"',  # preInstall 时目录尚未建
                   'echo PRE %VERSION% > "%INSTALL_DIR%' + _BS + 'pre.txt"', "exit /b 0"])
    _write_script(cfg, "scripts/post.bat",
                  ["@echo off", 'call "%~dp0sub' + _BS + 'helper.bat"',
                   'echo POST > "%INSTALL_DIR%' + _BS + 'post.txt"', "exit /b 0"])
    _write_script(cfg, "scripts/sub/helper.bat",
                  ["@echo off", 'echo H > "%INSTALL_DIR%' + _BS + 'helper.txt"', "exit /b 0"])

    hooks = _hooks_yaml(
        pre=["{ path: scripts/pre.bat, onFailure: abort, timeoutSec: 60 }"],
        post=["{ path: scripts/post.bat, onFailure: continue, timeoutSec: 60, "
              "keep: true, keepDir: '%INSTALL_DIR%" + _BS + "kept' }"])
    packager.write_yaml(cfg, product=product["name"], hooks_yaml=hooks)
    setup = case_dir / "Setup.exe"
    res = packager.run_packager(app_root, cfg, setup)
    assert res.returncode == 0, res.stdout + res.stderr

    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))
    rc, _ = installer.silent_install(setup, dst)
    assert rc == 0
    assert (dst / "pre.txt").read_text(encoding="utf-8").strip() == "PRE 1.0.0"  # VERSION 注入
    assert (dst / "post.txt").exists()
    assert (dst / "helper.txt").exists()                       # 兄弟脚本被调用
    assert (dst / "kept" / "post.bat").exists()                # keep 保留主脚本
    assert (dst / "kept" / "sub" / "helper.bat").exists()      # keep 保留兄弟脚本


@pytest.mark.hooks
def test_preinstall_abort_rolls_back(make_package, case_dir, product):
    """preInstall 以 onFailure=abort 失败 → 安装中止，不产生安装目录/注册表。"""
    from helpers import packager
    app_root = case_dir / "input"
    corpus.make_payload(app_root, dirs=1, files_per_dir=3)
    cfg = case_dir / "config"
    packager.prepare_config(cfg)
    _write_script(cfg, "scripts/fail.bat", ["@echo off", "exit /b 7"])  # 非零退出
    hooks = _hooks_yaml(pre=["{ path: scripts/fail.bat, onFailure: abort, timeoutSec: 60 }"])
    packager.write_yaml(cfg, product=product["name"], hooks_yaml=hooks)
    setup = case_dir / "Setup.exe"
    assert packager.run_packager(app_root, cfg, setup).returncode == 0

    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))
    rc, _ = installer.silent_install(setup, dst)
    assert rc != 0                                       # 安装失败
    # 回滚契约：不产生 ARP 卸载入口 / manifest / 自启，产品状态非 "installed"。
    # 注：preInstall abort 发生在解压前，无已装文件可回滚；引擎会把先前写入的
    # "installing" 产品状态标记为 "install_failed"（保留键，供诊断/下次探测），
    # 因此这里不断言产品键清空。
    assert sysstate.arp_entry(product["name"]) == {}
    assert not (dst / "install.manifest.json").exists()
    assert sysstate.autostart_value(product["name"]) is None
    assert sysstate.product_reg(product["name"]).get("InstallState") != "installed"
