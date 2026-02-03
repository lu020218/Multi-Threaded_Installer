# 打包稳定性基准报告（P0）

执行日期：2026-02-03  
执行环境：Windows（本仓本地）  
输入目录：`examples/yaml-flow-demo`

## 执行命令

```powershell
cargo run -p packager_cli -- --input ./examples/yaml-flow-demo --output ./target/determinism/a.pkg --package-only
cargo run -p packager_cli -- --input ./examples/yaml-flow-demo --output ./target/determinism/b.pkg --package-only
Get-FileHash ./target/determinism/a.pkg -Algorithm SHA256
Get-FileHash ./target/determinism/b.pkg -Algorithm SHA256
```

## 结果

- `a.pkg` SHA256: `E3777F1012B57EBF841D08EA5E4C2B5838C8FD80C9F58176FC134C384F5AD794`
- `b.pkg` SHA256: `E3777F1012B57EBF841D08EA5E4C2B5838C8FD80C9F58176FC134C384F5AD794`
- 结论：同一输入在相同配置下生成包字节一致，满足当前 P0 稳定性要求。

## 注意事项

- 输出文件必须位于输入目录之外，避免产物被再次扫描进输入导致文件集合变化。
- 若后续引入时间戳、随机盐等字段，需同步更新该报告与测试策略。
