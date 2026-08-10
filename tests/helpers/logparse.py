# -*- coding: utf-8 -*-
"""安装器日志解析：定位最新日志、抽取 TimingSummary / framed install / snapshot 行。"""
import re

from . import env


def latest_log(product: str):
    """返回该产品最新一份安装器日志路径（无则 None）。"""
    pattern = "MTInstaller_%s_*.log" % product
    logs = sorted(env.INSTALLER_LOG_DIR.glob(pattern),
                  key=lambda p: p.stat().st_mtime, reverse=True)
    return logs[0] if logs else None


def read_latest(product: str) -> str:
    p = latest_log(product)
    return p.read_text(encoding="utf-8", errors="replace") if p else ""


def framed_lines(text: str):
    """返回 [(folder, frames, workers, skipped, written), ...]。"""
    out = []
    for m in re.finditer(
        r"framed install folder=(\S+) frames=(\d+) workers=(\d+) skipped=(\d+) written=(\d+)",
        text,
    ):
        out.append((m.group(1), int(m.group(2)), int(m.group(3)),
                    int(m.group(4)), int(m.group(5))))
    return out


def snapshot_version(text: str):
    m = re.search(r"snapshot taken found=(\w+).*?version=(\S*)", text)
    if not m:
        return None
    return (m.group(1) == "true", m.group(2))


def timing_stages(text: str):
    """返回 stages 行的 {阶段: 毫秒} 字典。"""
    m = re.search(r"stages: (.+)", text)
    if not m:
        return {}
    out = {}
    for kv in m.group(1).split():
        if "=" in kv and kv.endswith("ms"):
            k, v = kv.split("=")
            out[k] = int(v[:-2])
    return out


def migration_from(text: str):
    m = re.search(r"Applying migration at [\d.]+ \(from ([\d.]+)\)", text)
    return m.group(1) if m else None
