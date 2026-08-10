# -*- coding: utf-8 -*-
"""升级：同内容全跳过、增量精准写、版本门控、注册表优先+manifest 双读。"""
import winreg

import pytest

from helpers import corpus, installer, logparse, sysstate, env


def _install_v(make_package, app_root, product, dst, version):
    setup, res = make_package(app_root, version=version,
                              output_name="Setup_%s.exe" % version.replace(".", "_"))
    assert res.returncode == 0, res.stdout + res.stderr
    rc, frames = installer.silent_install(setup, dst)
    return rc, frames


@pytest.mark.upgrade
def test_same_content_all_skipped(make_package, case_dir, product):
    app_root = case_dir / "input"
    app = corpus.make_payload(app_root, dirs=5, files_per_dir=40)
    n = corpus.count_files(app)
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))

    assert _install_v(make_package, app_root, product, dst, "1.0.0")[0] == 0
    assert _install_v(make_package, app_root, product, dst, "2.0.0")[0] == 0

    framed = logparse.framed_lines(logparse.read_latest(product["name"]))
    total_skipped = sum(f[3] for f in framed)
    total_written = sum(f[4] for f in framed)
    assert total_skipped == n and total_written == 0, framed


@pytest.mark.upgrade
def test_incremental_precise_write(make_package, case_dir, product):
    app_root = case_dir / "input"
    app = corpus.make_payload(app_root, dirs=5, files_per_dir=40)
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))

    assert _install_v(make_package, app_root, product, dst, "1.0.0")[0] == 0
    # 改 3 个文件
    corpus.touch_change(app / "pkg00" / "i000.cfg")
    corpus.touch_change(app / "pkg02" / "i010.cfg")
    corpus.touch_change(app / "pkg04" / "i039.cfg")
    assert _install_v(make_package, app_root, product, dst, "2.0.0")[0] == 0

    framed = logparse.framed_lines(logparse.read_latest(product["name"]))
    assert sum(f[4] for f in framed) == 3, framed
    assert sysstate.tree_mismatches(app, dst) == []


@pytest.mark.upgrade
def test_same_version_rejected(make_package, case_dir, product):
    app_root = case_dir / "input"
    corpus.make_payload(app_root, dirs=1, files_per_dir=3)
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))
    assert _install_v(make_package, app_root, product, dst, "1.0.0")[0] == 0
    # 同版本再装 → 拒绝（exit != 0）
    rc, _ = _install_v(make_package, app_root, product, dst, "1.0.0")
    assert rc != 0


@pytest.mark.upgrade
def test_registry_version_priority(make_package, case_dir, product):
    """注册表 Version 优先于 manifest：把注册表版本改低，同版本包应放行升级。"""
    app_root = case_dir / "input"
    corpus.make_payload(app_root, dirs=1, files_per_dir=3)
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))
    assert _install_v(make_package, app_root, product, dst, "2.0.0")[0] == 0

    # 注册表 Version 改成 0.5.0（低于包版本 2.0.0）
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"Software\%s" % product["name"], 0,
                        winreg.KEY_SET_VALUE | winreg.KEY_WOW64_64KEY) as k:
        winreg.SetValueEx(k, "Version", 0, winreg.REG_SZ, "0.5.0")

    rc, _ = _install_v(make_package, app_root, product, dst, "2.0.0")
    assert rc == 0  # 注册表 0.5.0 < 包 2.0.0 → 放行
    assert logparse.migration_from(logparse.read_latest(product["name"])) == "0.5.0"
