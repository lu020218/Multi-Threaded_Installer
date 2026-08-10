# -*- coding: utf-8 -*-
"""测试语料生成：可压缩文本群、大文件（独帧）、随机二进制、中文路径等。
确定性（固定种子），使全树 md5 断言可重复。"""
import os
import pathlib
import random


def make_payload(root: pathlib.Path, *, dirs=8, files_per_dir=40, seed=1,
                 big_files=None, chinese=False):
    """在 root/app 下生成可压缩文本语料 + 可选大文件/中文路径。返回 app 目录。"""
    app = root / "app"
    app.mkdir(parents=True, exist_ok=True)
    rng = random.Random(seed)
    words = "config module render layout handler service cache token widget panel".split()
    for d in range(dirs):
        sub = app / ("pkg%02d" % d)
        sub.mkdir(exist_ok=True)
        for f in range(files_per_dir):
            lines = ["%s.%s = %d" % (rng.choice(words), rng.choice(words), rng.randint(0, 99999))
                     for _ in range(rng.randint(20, 80))]
            (sub / ("i%03d.cfg" % f)).write_text("\n".join(lines), encoding="utf-8")
    for name, size in (big_files or {}).items():
        (app / name).write_bytes(os.urandom(size))
    if chinese:
        cn = app / "中文目录"
        cn.mkdir(exist_ok=True)
        (cn / "配置文件.txt").write_text("中文内容测试\n" * 20, encoding="utf-8")
    return app


def make_plugins(root: pathlib.Path, component_exe: pathlib.Path = None,
                 component_rel="chrome/ChromeSetup.exe"):
    """生成 plugins 目录；给定 component_exe 时放入指定组件安装器路径。"""
    plugins = root / "plugins"
    plugins.mkdir(parents=True, exist_ok=True)
    if component_exe is not None:
        dst = plugins / component_rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(pathlib.Path(component_exe).read_bytes())
    return plugins


def count_files(app: pathlib.Path) -> int:
    return sum(len(fs) for _, _, fs in os.walk(app))


def touch_change(path: pathlib.Path, extra=b"\nCHANGED\n"):
    """对已有文件追加内容（增量升级用）。"""
    with open(path, "ab") as f:
        f.write(extra)
