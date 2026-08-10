# -*- coding: utf-8 -*-
"""语言参数：--language 归一、注册表落盘、升级继承与显式覆盖。"""
import json

import pytest

from helpers import corpus, installer, sysstate


@pytest.mark.language
def test_language_normalized_and_written(make_package, case_dir, product):
    app_root = case_dir / "input"
    app = corpus.make_payload(app_root, dirs=1, files_per_dir=3)
    setup, res = make_package(app_root, version="1.0.0")
    assert res.returncode == 0
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))
    # en-US 应归一为 en_US
    rc, _ = installer.silent_install(setup, dst, language="en-US")
    assert rc == 0
    assert sysstate.product_reg(product["name"]).get("Language") == "en_US"
    manifest = json.loads((dst / "install.manifest.json").read_text(encoding="utf-8"))
    assert manifest["language"] == "en_US"


@pytest.mark.language
def test_upgrade_inherit_then_override(make_package, case_dir, product):
    app_root = case_dir / "input"
    corpus.make_payload(app_root, dirs=1, files_per_dir=3)
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))

    s1, _ = make_package(app_root, version="1.0.0", output_name="v1.exe")
    assert installer.silent_install(s1, dst, language="en_US")[0] == 0

    # 升级不带 --language → 继承旧语言 en_US
    s2, _ = make_package(app_root, version="2.0.0", output_name="v2.exe")
    assert installer.silent_install(s2, dst, upgrade=True)[0] == 0
    assert sysstate.product_reg(product["name"]).get("Language") == "en_US"

    # 升级带 --language zh_CN → 覆盖
    s3, _ = make_package(app_root, version="3.0.0", output_name="v3.exe")
    assert installer.silent_install(s3, dst, upgrade=True, language="zh_CN")[0] == 0
    assert sysstate.product_reg(product["name"]).get("Language") == "zh_CN"
