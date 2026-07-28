# -*- coding: utf-8 -*-
"""公司编码规范检查：禁止使用非安全版 CRT 函数（memcpy/strcpy/sprintf/getenv 等）。

用法：python tools/check_banned_functions.py
扫描 src/ 与 include/（不含 third_party），命中任一禁用函数即非零退出。
可挂 pre-commit 或 CI；本地随手跑也行。
"""
import os
import re
import sys

# 禁用函数 → 建议替代。命中裸调用（后随 '('）即违规；对应 _s 安全版不受影响。
BANNED = {
    "memcpy": "memcpy_s",
    "memmove": "memmove_s",
    "strcpy": "strcpy_s",
    "strncpy": "strncpy_s",
    "strcat": "strcat_s",
    "strncat": "strncat_s",
    "sprintf": "sprintf_s",
    "vsprintf": "vsprintf_s",
    "_snprintf": "_snprintf_s",
    "swprintf": "swprintf_s",
    "wcscpy": "wcscpy_s",
    "wcsncpy": "wcsncpy_s",
    "wcscat": "wcscat_s",
    "wcsncat": "wcsncat_s",
    "sscanf": "sscanf_s",
    "swscanf": "swscanf_s",
    "strtok": "strtok_s",
    "wcstok": "wcstok_s",
    "getenv": "GetEnvironmentVariableA/W 或 _dupenv_s",
    "_wgetenv": "GetEnvironmentVariableW 或 _wdupenv_s",
    "gets": "gets_s",
    "itoa": "_itoa_s",
    "_itoa": "_itoa_s",
    "localtime": "localtime_s",
    "gmtime": "gmtime_s",
    "ctime": "ctime_s",
    "asctime": "asctime_s",
}

SCAN_DIRS = ("src", "include")
EXTS = (".cpp", ".h", ".hpp", ".c")

# 形如 `memcpy(` 或 `std::memcpy (`，且后一个字符不是构成 `_s` 变体的一部分。
PATTERN = re.compile(
    r"(?<![A-Za-z0-9_])(?:std::)?(" + "|".join(re.escape(f) for f in BANNED) + r")\s*\("
)


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    violations = []
    for scan in SCAN_DIRS:
        base = os.path.join(root, scan)
        for dirpath, _, names in os.walk(base):
            for name in names:
                if not name.endswith(EXTS):
                    continue
                path = os.path.join(dirpath, name)
                try:
                    with open(path, encoding="utf-8", errors="replace") as f:
                        for lineno, line in enumerate(f, 1):
                            stripped = line.lstrip()
                            if stripped.startswith("//") or stripped.startswith("*"):
                                continue  # 注释行不计
                            for m in PATTERN.finditer(line):
                                fn = m.group(1)
                                violations.append(
                                    (os.path.relpath(path, root), lineno, fn, BANNED[fn])
                                )
                except OSError as ex:
                    print(f"WARN: cannot read {path}: {ex}", file=sys.stderr)

    if violations:
        print("编码规范违规：检测到禁用的非安全版函数调用：\n")
        for path, lineno, fn, hint in violations:
            print(f"  {path}:{lineno}: {fn}()  ->  请改用 {hint}")
        print(f"\n共 {len(violations)} 处。", file=sys.stderr)
        return 1
    print("check_banned_functions: PASS (0 violations)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
