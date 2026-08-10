# -*- coding: utf-8 -*-
"""打包器行为：必填校验拦截、坏 yaml、分帧聚合、图标嵌入。"""
import pytest

from helpers import corpus, packager


@pytest.mark.packager
def test_missing_appname_rejected(case_dir, product):
    app = corpus.make_payload(case_dir / "input", dirs=1, files_per_dir=2)
    cfg = case_dir / "config"
    packager.prepare_config(cfg)
    # 手写缺 appName 的 yaml
    (cfg / "packager.yaml").write_text(
        "app:\r\n  productName: %s\r\n  appId: com.t.x\r\n  publisher: T\r\n"
        "  version: 1.0.0\r\n  defaultDir: '%%ProgramFiles%%'\r\n"
        "package:\r\n  compression: { algorithm: xz, level: 1 }\r\n"
        "  layout:\r\n    - { source: app, target: '%%InstallDir%%' }\r\n" % product["name"],
        encoding="utf-8", newline="")
    res = packager.run_packager(case_dir / "input", cfg, case_dir / "out.exe")
    assert res.returncode != 0
    assert "appName" in (res.stdout + res.stderr)


@pytest.mark.packager
def test_appname_with_exe_suffix_rejected(case_dir, product):
    corpus.make_payload(case_dir / "input", dirs=1, files_per_dir=2)
    cfg = case_dir / "config"
    packager.prepare_config(cfg)
    packager.write_yaml(cfg, product=product["name"], app_name="tsmain.exe")
    res = packager.run_packager(case_dir / "input", cfg, case_dir / "out.exe")
    assert res.returncode != 0
    assert ".exe" in (res.stdout + res.stderr)


@pytest.mark.packager
def test_full_package_with_icon_and_frames(make_package, case_dir):
    app_root = case_dir / "input"
    corpus.make_payload(app_root, dirs=4, files_per_dir=30,
                        big_files={"big.bin": 5 * 1024 * 1024})
    setup, res = make_package(app_root, with_icon=True)
    assert res.returncode == 0, res.stdout + res.stderr
    assert setup.exists() and setup.stat().st_size > 0
    # 分帧默认开启：日志含 per-file-frames（大文件独帧 + 小文件聚合）
    assert "mode=per-file-frames" in res.stdout
