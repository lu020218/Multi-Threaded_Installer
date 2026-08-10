# -*- coding: utf-8 -*-
"""打包器封装：生成 packager.yaml（单引号防 yaml 转义坑）、调用 packager.exe。"""
import pathlib
import shutil
import subprocess

from . import env


def write_yaml(config_dir: pathlib.Path, *, product, app_name="tsmain",
               app_id="com.test.auto", publisher="AutoTest", version="1.0.0",
               default_dir="%ProgramFiles%", icon=None, level=1,
               layout=None, hooks_yaml="", extra_app_lines=""):
    """写 packager.yaml。layout 为 [(source,target),...]；hooks_yaml 为已缩进好的 hooks 段。
    统一用单引号包裹含反斜杠/%的值，规避 yaml-cpp 双引号转义报错。"""
    if layout is None:
        layout = [("app", "%InstallDir%")]
    lines = [
        "app:",
        "  productName: %s" % product,
        "  appName: %s" % app_name,
        "  appId: %s" % app_id,
        "  publisher: %s" % publisher,
        "  version: %s" % version,
        "  defaultDir: '%s'" % default_dir,
    ]
    if icon:
        lines.append("  icon: %s" % icon)
    if extra_app_lines:
        lines.extend(extra_app_lines.splitlines())
    lines += [
        "package:",
        "  compression: { algorithm: xz, level: %d }" % level,
        "  layout:",
    ]
    for src, tgt in layout:
        lines.append("    - { source: %s, target: '%s' }" % (src, tgt))
    text = "\r\n".join(lines) + "\r\n"
    if hooks_yaml:
        text += hooks_yaml if hooks_yaml.endswith("\r\n") else (hooks_yaml + "\r\n")
    (config_dir / "packager.yaml").write_text(text, encoding="utf-8", newline="")


def prepare_config(config_dir: pathlib.Path, *, with_icon=False):
    """准备 config 目录：拷贝 resources（GUI 资源必需）+ 可选图标。"""
    config_dir.mkdir(parents=True, exist_ok=True)
    dst_res = config_dir / "resources"
    if not dst_res.exists():
        shutil.copytree(env.RESOURCES_DIR, dst_res)
    if with_icon and env.APP_ICON.exists():
        shutil.copy(env.APP_ICON, config_dir / "app.ico")


def run_packager(input_dir, config_dir, output_exe, *, env_overrides=None):
    """调用 packager.exe，返回 CompletedProcess。"""
    import os
    run_env = os.environ.copy()
    if env_overrides:
        run_env.update(env_overrides)
    return subprocess.run(
        [str(env.PACKAGER_EXE), "-i", str(input_dir), "-c", str(config_dir),
         "-o", str(output_exe)],
        capture_output=True, text=True, env=run_env, timeout=1800,
    )
