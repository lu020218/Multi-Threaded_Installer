# -*- coding: utf-8 -*-
"""会话级前置检查 + 每用例产品隔离 + 失败兜底清扫 + 报告增强。"""
import pathlib
import shutil
import time

import pytest

from helpers import env, sysstate, packager


def pytest_configure(config):
    if not env.is_admin():
        pytest.exit("安装器测试需要管理员权限运行（读写 HKLM/桌面）。", returncode=2)
    missing = env.require_binaries()
    if missing:
        pytest.exit("缺少必需二进制（先构建 Release）：\n  " + "\n  ".join(missing),
                    returncode=2)


@pytest.fixture(scope="session")
def workdir(tmp_path_factory):
    """会话级工作根：所有打包/安装临时产物落这里。"""
    d = tmp_path_factory.mktemp("mti_auto")
    yield d


@pytest.fixture
def product(request):
    """每用例独立产品名（MtiAutoTest_<用例名>），并在结束/失败时强制清扫其系统状态。
    返回一个 dict 上下文，测试按需填 install_dirs 以便兜底删除安装目录。"""
    raw = request.node.name
    safe = "".join(c if c.isalnum() else "_" for c in raw)[:40]
    name = "%s_%s" % (env.PRODUCT_PREFIX, safe)
    ctx = {"name": name, "install_dirs": []}

    # 前置清扫，避免上一次残留干扰。
    sysstate.force_cleanup(name)
    yield ctx
    # 后置：先尝试正常卸载（若装了 uninstall.exe），再强制清扫。
    for d in ctx["install_dirs"]:
        un = pathlib.Path(d) / "uninstall.exe"
        if un.exists():
            try:
                import subprocess
                subprocess.run([str(un), "--silent"], capture_output=True, timeout=300)
                time.sleep(1.5)
            except Exception:
                pass
    sysstate.force_cleanup(name, ctx["install_dirs"])


@pytest.fixture
def case_dir(workdir, request):
    """每用例独立目录（input/config/output）。"""
    safe = "".join(c if c.isalnum() else "_" for c in request.node.name)[:40]
    d = workdir / safe
    d.mkdir(parents=True, exist_ok=True)
    yield d
    shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def make_package(case_dir, product):
    """便捷打包器：build_package(app_dir, **yaml_kwargs) -> setup_exe 路径。"""
    def _build(input_root, *, version="1.0.0", with_icon=False, output_name=None, **yaml_kw):
        config_dir = case_dir / "config"
        packager.prepare_config(config_dir, with_icon=with_icon)
        packager.write_yaml(config_dir, product=product["name"], version=version,
                            icon=("app.ico" if with_icon else None), **yaml_kw)
        out = case_dir / (output_name or ("Setup_%s.exe" % version.replace(".", "_")))
        res = packager.run_packager(input_root, config_dir, out)
        return out, res
    return _build


# ── 报告增强 ────────────────────────────────────────────────────────────────
@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    setattr(item, "rep_" + rep.when, rep)
    # 把用例 docstring 作为报告里的说明。
    if rep.when == "call":
        doc = (item.function.__doc__ or "").strip().splitlines()
        rep.description = doc[0] if doc else ""


def pytest_html_report_title(report):
    report.title = "Multi-Threaded Installer 自动化测试报告"


@pytest.hookimpl(optionalhook=True)
def pytest_html_results_summary(prefix, summary, postfix):
    import platform
    prefix.extend([
        "<p><b>被测二进制:</b> %s</p>" % env.RELEASE_DIR,
        "<p><b>平台:</b> %s / Python %s</p>" % (platform.platform(), platform.python_version()),
        "<p><b>管理员:</b> %s</p>" % ("是" if env.is_admin() else "否"),
    ])


# 说明列：pytest-html 4.x 自动展示 report.description（上面 makereport 已赋值），
# 无需自定义 py.xml 表格 hook（该 API 已在 4.x 移除）。
