# -*- coding: utf-8 -*-
"""组件安装：引擎注册表 chrome 槽（plugins/chrome/ChromeSetup.exe）。
选中默认装、缺失跳过、--components 显式空集仅 required（chrome 非 required → 跳过）。"""
import pytest

from helpers import corpus, installer, logparse, env


def _pkg_with_chrome(make_package, case_dir, *, with_component: bool):
    app_root = case_dir / "input"
    corpus.make_payload(app_root, dirs=1, files_per_dir=3)
    plugins = corpus.make_plugins(
        app_root,
        component_exe=(env.REPO_ROOT / "tests" / "assets" / "fake_component.exe")
        if with_component else None)
    if not with_component:
        (plugins / "keep.txt").write_text("x", encoding="utf-8")  # 非空 folder
    setup, res = make_package(
        app_root,
        layout=[("app", "%InstallDir%"), ("plugins", "%InstallDir%\\plugins")])
    assert res.returncode == 0, res.stdout + res.stderr
    return setup


@pytest.mark.components
def test_default_component_installed(make_package, case_dir, product):
    setup = _pkg_with_chrome(make_package, case_dir, with_component=True)
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))
    assert installer.silent_install(setup, dst)[0] == 0
    log = logparse.read_latest(product["name"])
    assert "Component installed: chrome" in log or "status=installed" in log


@pytest.mark.components
def test_missing_component_skipped(make_package, case_dir, product):
    setup = _pkg_with_chrome(make_package, case_dir, with_component=False)
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))
    assert installer.silent_install(setup, dst)[0] == 0  # 非 required：缺失仅跳过
    log = logparse.read_latest(product["name"])
    assert "skipped(missing)" in log or "not found" in log


@pytest.mark.components
def test_components_empty_selects_required_only(make_package, case_dir, product):
    """--components "" → 仅装 required；chrome 非 required 应被跳过（not-selected）。"""
    setup = _pkg_with_chrome(make_package, case_dir, with_component=True)
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))
    assert installer.silent_install(setup, dst, components="")[0] == 0
    log = logparse.read_latest(product["name"])
    assert "skipped(not-selected)" in log
