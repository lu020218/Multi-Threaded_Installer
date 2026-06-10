# ── preInstall 样例脚本（PowerShell）──────────────────────────────────────
# 契约见 docs/USER_GUIDE.md §7：
#   · 退出码 0 = 成功，非 0 / 超时 = 失败 → 走该脚本配置的 onFailure；
#   · 可读环境变量 $env:INSTALL_DIR / $env:VERSION（引擎注入）；继承安装器管理员权限，勿再 elevate；
#   · 只做本次构建特有的收尾动作；跨版本兼容请走引擎迁移表，不要写在这里。
# 由引擎经 powershell -NoProfile -ExecutionPolicy Bypass -File 运行。
# preInstall 在解压之前运行，此时安装目录可能尚不存在。

Write-Host "[pre_install.ps1] INSTALL_DIR=$env:INSTALL_DIR VERSION=$env:VERSION"

# 在此添加你的安装前业务逻辑……

exit 0
