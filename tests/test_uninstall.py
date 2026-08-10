# -*- coding: utf-8 -*-
"""卸载：全清五项、旧 schema manifest 兼容。"""
import json

import pytest

from helpers import corpus, installer, sysstate


@pytest.mark.uninstall
def test_uninstall_full_sweep(make_package, case_dir, product):
    app_root = case_dir / "input"
    corpus.make_payload(app_root, dirs=2, files_per_dir=10)
    (app_root / "app" / "tsmain.exe").write_bytes(b"MZ" + b"\x00" * 512)
    setup, res = make_package(app_root)
    assert res.returncode == 0
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))
    assert installer.silent_install(setup, dst, auto_startup=True, desktop_icon=True)[0] == 0
    assert sysstate.product_reg(product["name"])  # 装好了

    assert installer.silent_uninstall(dst / "uninstall.exe") == 0
    assert sysstate.residue(product["name"]) == []   # 五项全清
    assert not (dst / "install.manifest.json").exists()


@pytest.mark.uninstall
def test_legacy_schema_manifest_uninstall(make_package, case_dir, product):
    """把新 manifest 降级成旧 schema（顶层 appId/appVersion + scope），卸载应仍能全清。"""
    app_root = case_dir / "input"
    corpus.make_payload(app_root, dirs=1, files_per_dir=5)
    setup, res = make_package(app_root)
    assert res.returncode == 0
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))
    assert installer.silent_install(setup, dst)[0] == 0

    mpath = dst / "install.manifest.json"
    m = json.loads(mpath.read_text(encoding="utf-8"))
    del m["app"]
    m["manifestVersion"] = 3
    m["appId"] = product["name"]
    m["appName"] = product["name"]
    m["displayName"] = product["name"]
    m["appVersion"] = "1.0.0"
    m["publisher"] = "AutoTest"
    for e in m["cleanup"]["uninstallEntries"]:
        e["scope"] = 1
    mpath.write_text(json.dumps(m, indent=2), encoding="utf-8")

    assert installer.silent_uninstall(dst / "uninstall.exe") == 0
    assert sysstate.residue(product["name"]) == []
