# -*- coding: utf-8 -*-
"""进度契约：落盘段中途有事件、全序列单调。"""
import pytest

from helpers import corpus, installer


@pytest.mark.progress
def test_fresh_progress_monotonic_with_midframes(make_package, case_dir, product):
    app_root = case_dir / "input"
    # 多个大文件 → 多帧，落盘段应出现中间刻度
    corpus.make_payload(app_root, dirs=6, files_per_dir=60,
                        big_files={"big1.bin": 8 * 1024 * 1024, "big2.bin": 6 * 1024 * 1024})
    setup, res = make_package(app_root, level=1)
    assert res.returncode == 0
    dst = case_dir / "dst"
    product["install_dirs"].append(str(dst))
    rc, frames = installer.silent_install(setup, dst)
    assert rc == 0

    pcts = installer.progress_percents(frames)
    assert pcts, "无任何进度帧"
    # 单调不回退
    assert all(b >= a for a, b in zip(pcts, pcts[1:])), pcts
    # 落盘段（25%~70%）出现中间刻度（不止起点/终点）
    mid = [p for p in pcts if 25 < p < 70]
    assert mid, "落盘段无中间进度事件: %r" % pcts
