# -*- coding: utf-8 -*-
"""锁定文件：升级时锁定的 .exe 走 pending-replace + rebootRequired（exit=4）。"""
import subprocess
import time

import pytest

from helpers import corpus, installer, logparse, env


@pytest.mark.locked
@pytest.mark.skipif(not env.FILELOCK_EXE.exists(), reason="缺 FileLockSimulator.exe")
def test_locked_exe_pending_replace(make_package, case_dir, product):
    app_root = case_dir / "input"
    app = corpus.make_payload(app_root, dirs=1, files_per_dir=3)
    (app / "locked.exe").write_bytes(b"MZ" + b"\x00" * 4096)
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))

    s1, _ = make_package(app_root, version="1.0.0", output_name="v1.exe")
    assert installer.silent_install(s1, dst)[0] == 0

    # 改动 locked.exe，制作 v2
    corpus.touch_change(app / "locked.exe", extra=b"CHANGED2")
    s2, _ = make_package(app_root, version="2.0.0", output_name="v2.exe")

    # 独占锁定已装的 locked.exe，再升级
    lock = subprocess.Popen([str(env.FILELOCK_EXE), "--mode=both",
                             "--path=%s" % (dst / "locked.exe"), "--share=none"],
                            stdin=subprocess.PIPE)
    try:
        time.sleep(2)
        rc, _ = installer.silent_install(s2, dst)
        # 锁定 .exe → 计划重启替换：退出码 4（rebootRequired）
        assert rc == 4, "锁定 exe 升级应返回 rebootRequired(4)，实际 %d" % rc
        log = logparse.read_latest(product["name"])
        assert "rebootRequired=true" in log
        assert "RebootReplace] registered" in log
    finally:
        lock.terminate()
        try:
            lock.wait(timeout=10)
        except Exception:
            lock.kill()
        time.sleep(1)
