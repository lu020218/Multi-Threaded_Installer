# -*- coding: utf-8 -*-
"""编码规范：禁用函数扫描（复用 tools/check_banned_functions.py）。"""
import subprocess
import sys

import pytest

from helpers import env


@pytest.mark.standard
def test_no_banned_functions():
    script = env.REPO_ROOT / "tools" / "check_banned_functions.py"
    assert script.exists(), "缺少 tools/check_banned_functions.py"
    res = subprocess.run([sys.executable, str(script)], capture_output=True, text=True)
    assert res.returncode == 0, res.stdout + res.stderr
