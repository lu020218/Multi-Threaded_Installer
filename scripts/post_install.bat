@echo off
rem ── postInstall 样例脚本（bat）────────────────────────────────────────────
rem 契约见 docs/USER_GUIDE.md §7：
rem   · 退出码 0 = 成功，非 0 / 超时 = 失败 → 走该脚本配置的 onFailure；
rem   · 可读环境变量 INSTALL_DIR / VERSION（引擎注入）；继承安装器管理员权限，勿再 elevate；
rem   · 只做本次构建特有的收尾动作；跨版本兼容请走引擎迁移表，不要写在这里。
rem postInstall 在安装收尾之后运行，此时安装目录已就绪。

echo [post_install.bat] INSTALL_DIR=%INSTALL_DIR% VERSION=%VERSION%

rem 在此添加你的安装后业务逻辑……

exit /b 0
