# -*- coding: utf-8 -*-
"""真实包全周期（marked real，默认不跑）：需环境变量 MTI_REAL_INPUT 指向真实 input 目录。
run_tests.bat --real 触发。打包 v1/v2 → 全新装 → 升级全跳过 → 卸载全清。"""
import os
import pathlib
import shutil

import pytest

from helpers import env, packager, installer, logparse, sysstate


@pytest.mark.real
@pytest.mark.slow
def test_real_package_full_cycle(case_dir, product):
    real_input = os.environ.get("MTI_REAL_INPUT")
    if not real_input or not pathlib.Path(real_input).is_dir():
        pytest.skip("未设置 MTI_REAL_INPUT（真实包 input 目录）")

    cfg = case_dir / "config"
    packager.prepare_config(cfg, with_icon=env.APP_ICON.exists())
    # 真实包按其目录结构声明 layout；这里以常见的 app + plugins 双 folder 为例。
    layout = [("app", "%InstallDir%")]
    if (pathlib.Path(real_input) / "plugins").is_dir():
        layout.append(("plugins", "%ProgramData%\\" + product["name"] + "\\plugins"))

    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))

    def _pack(version, out):
        packager.write_yaml(cfg, product=product["name"], version=version,
                            icon=("app.ico" if env.APP_ICON.exists() else None),
                            layout=layout, level=1)  # level 1 加速真实包评测
        return packager.run_packager(real_input, cfg, case_dir / out)

    assert _pack("1.0.0", "v1.exe").returncode == 0
    setup1 = case_dir / "v1.exe"
    assert _pack("2.0.0", "v2.exe").returncode == 0
    setup2 = case_dir / "v2.exe"

    assert installer.silent_install(setup1, dst, timeout=1800)[0] == 0
    # 升级同内容 → 全跳过
    assert installer.silent_install(setup2, dst, timeout=1800)[0] == 0
    framed = logparse.framed_lines(logparse.read_latest(product["name"]))
    assert framed and sum(f[4] for f in framed) == 0, "升级未全跳过: %r" % framed

    assert installer.silent_uninstall(dst / "uninstall.exe", timeout=1800) == 0
    assert sysstate.residue(product["name"]) == []
