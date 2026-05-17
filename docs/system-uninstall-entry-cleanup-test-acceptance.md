# systemUninstallEntry / cleanup 配置收敛测试验收方案

## 1. 验收目标

验证 v3 配置中系统卸载项的职责拆分与执行行为符合当前约定：

- `installer.systemUninstallEntry` 必填，安装完成后必须写入当前版本系统卸载项。
- `installer.cleanup.systemUninstallEntry.legacyEntries[]` 只在覆盖/升级安装前清理旧系统卸载项。
- `uninstaller.cleanup.systemUninstallEntry` 负责卸载时清理当前版本系统卸载项，并可配置 `legacyEntries[]` 清理历史残留。
- 系统卸载项删除只按 `DisplayName + scope` 精确匹配，不再按 appId、registry key、installDir、UninstallString 推导。
- 旧字段 `installer.systemUninstallEntry.enabled`、字符串型 `uninstaller.cleanup.systemUninstallEntry`、`uninstaller.cleanup.legacy.uninstallEntries` 均被拒绝。

## 2. 测试环境与准备

- Windows 10/11。
- 使用 Release 构建产物：
  - `build_codex\Release\packager.exe`
  - `build_codex\Release\installer.exe`
  - `build_codex\Release\uninstaller.exe`
- 使用 v3 配置目录，包含：
  - `packager.yaml`
  - `resources\`
  - `app.ico`
- 准备 payload 目录：
  - `app\MyDesktopApp.exe`
  - `resources\` 可为空或放少量测试文件。
- 所有注册表测试项建议使用测试命名空间：
  - `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\MTITest_*`
  - `HKCU\Software\MTITest_*`
  - 如测试 machine scope，需要管理员权限并使用 `HKLM` 对应路径。

## 3. 自动化回归验收

### TC-A01 构建通过

步骤：

1. 执行：
   ```powershell
   cmake --build build_codex --config Release
   ```

预期：

- 构建成功。
- `packager.exe`、`installer.exe`、`uninstaller.exe`、`SchemaRegressionTests.exe` 均生成。

### TC-A02 回归测试通过

步骤：

1. 执行：
   ```powershell
   build_codex\test\Release\SchemaRegressionTests.exe
   ```

预期：

- 所有测试通过。
- 至少覆盖以下用例：
  - `load_valid_schema`
  - `reject_legacy_system_uninstall_entry_fields`
  - `metadata_round_trip`
  - `package_manifest_builder_and_codec_round_trip`
  - `cleanup_upgrade_system_artifacts_executes_explicit_rules`
  - `delete_uninstall_entry_matches_display_name`
  - `uninstall_from_manifest_executes_explicit_cleanup`

## 4. 配置解析与校验测试

### TC-C01 合法 v3 配置可加载

配置片段：

```yaml
installer:
  systemUninstallEntry:
    scope: user
    displayName: MyDesktopApp
    publisher: MyCompany
  cleanup:
    systemUninstallEntry:
      legacyEntries:
        - displayName: MyDesktopApp Legacy
          scope: user

uninstaller:
  cleanup:
    systemUninstallEntry:
      scope: user
      displayName: MyDesktopApp
      legacyEntries:
        - displayName: MyDesktopApp Legacy
          scope: user
```

步骤：

1. 用该配置执行打包。

预期：

- 打包成功。
- 日志中无 schema 或字段校验错误。

### TC-C02 拒绝 `installer.systemUninstallEntry.enabled`

配置片段：

```yaml
installer:
  systemUninstallEntry:
    enabled: true
    scope: user
    displayName: MyDesktopApp
```

预期：

- 打包失败。
- 错误包含 `Unsupported field 'installer.systemUninstallEntry.enabled'`。

### TC-C03 拒绝字符串型 `uninstaller.cleanup.systemUninstallEntry`

配置片段：

```yaml
uninstaller:
  cleanup:
    systemUninstallEntry: auto
```

预期：

- 打包失败。
- 错误指向 `uninstaller.cleanup.systemUninstallEntry` 且说明 expected object。

### TC-C04 拒绝 `uninstaller.cleanup.legacy.uninstallEntries`

配置片段：

```yaml
uninstaller:
  cleanup:
    legacy:
      uninstallEntries:
        - name: MyDesktopApp Legacy
          scope: user
```

预期：

- 打包失败。
- 错误包含 `Use 'uninstaller.cleanup.systemUninstallEntry.legacyEntries' instead`。

### TC-C05 legacyEntries 缺少 displayName 失败

配置片段：

```yaml
installer:
  cleanup:
    systemUninstallEntry:
      legacyEntries:
        - scope: user
```

预期：

- 打包失败。
- 错误包含 `displayName`。

### TC-C06 legacyEntries scope 为 auto/any 失败

配置片段：

```yaml
installer:
  cleanup:
    systemUninstallEntry:
      legacyEntries:
        - displayName: MyDesktopApp Legacy
          scope: auto
```

预期：

- 打包失败。
- 错误说明 legacy scope 只允许 `user / machine / both`。

### TC-C07 requireAdmin=false 拒绝 machine/both 当前系统卸载项

配置片段：

```yaml
installer:
  requireAdmin: false
  systemUninstallEntry:
    scope: machine
    displayName: MyDesktopApp
```

预期：

- 打包失败。
- 错误包含 `installer.requireAdmin=false cannot create machine system uninstall entry`。

## 5. Manifest 与快照测试

### TC-M01 PackageManifest round-trip 保留 installer cleanup legacyEntries

步骤：

1. 使用含 `installer.cleanup.systemUninstallEntry.legacyEntries[]` 的配置打包。
2. 通过现有 `SchemaRegressionTests` 或调试工具反序列化 package manifest。

预期：

- `manifest.install.cleanup.systemUninstallEntry.legacyEntries[]` 存在。
- `displayName` 和 `scope` 与配置一致。

### TC-M02 PackageManifest round-trip 保留 uninstaller cleanup systemUninstallEntry

步骤：

1. 使用含 `uninstaller.cleanup.systemUninstallEntry` 的配置打包。
2. 反序列化 package manifest。

预期：

- `manifest.lifecycle.uninstaller.cleanup.systemUninstallEntry.displayName` 与配置一致。
- `legacyEntries[]` 与配置一致。

### TC-M03 安装完成写入本地 manifest 快照

步骤：

1. 安装成功后打开 `%InstallDir%\install.manifest.json`。

预期：

- `installer.systemUninstallEntry` 存在，记录当前版本配置。
- `uninstaller.cleanup.actual.systemUninstallEntry[]` 存在，记录实际写入的系统卸载项。
- `uninstaller.cleanup.systemUninstallEntry.legacyEntries[]` 存在，记录卸载期 legacy 清理配置。
- 不出现 `uninstaller.cleanup.legacy.uninstallEntries`。

## 6. 覆盖/升级安装测试

### TC-U01 覆盖安装前清理 installer legacy 系统卸载项

准备：

1. 手动创建旧卸载项：
   - key：`HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\MTITest_OldKey`
   - `DisplayName=MyDesktopApp Legacy`
2. 当前配置：
   ```yaml
   installer:
     cleanup:
       systemUninstallEntry:
         legacyEntries:
           - displayName: MyDesktopApp Legacy
             scope: user
   ```

步骤：

1. 执行覆盖安装。

预期：

- 覆盖/升级清理阶段删除 `DisplayName=MyDesktopApp Legacy` 的卸载项。
- 如果 key 名不是 `MyDesktopApp Legacy`，仍能删除。
- 日志出现旧卸载项清理阶段，但不依赖 key 名。

### TC-U02 覆盖安装不清理 uninstaller legacyEntries

准备：

1. 手动创建旧卸载项：
   - `DisplayName=Only Uninstaller Legacy`
2. 只在 `uninstaller.cleanup.systemUninstallEntry.legacyEntries[]` 配置该 displayName，不在 `installer.cleanup` 配置。

步骤：

1. 执行覆盖安装。

预期：

- `Only Uninstaller Legacy` 不被覆盖/升级安装清理。

### TC-U03 升级安装无旧 manifest 时仍按 installer cleanup 清理

准备：

1. 通过 `installer.installState.detect` 能发现旧安装目录。
2. 旧目录下没有 `install.manifest.json`。
3. 配置 `installer.cleanup.systemUninstallEntry.legacyEntries[]`。

步骤：

1. 执行：
   ```powershell
   installer.exe --upgrade
   ```

预期：

- 安装目录固定为 detect 发现的旧目录。
- 旧系统卸载项按 installer cleanup 删除。
- 不要求旧 manifest 可读。

## 7. 卸载测试

### TC-R01 manifest 可读时清理实际写入的当前系统卸载项

步骤：

1. 完成安装。
2. 确认系统卸载列表存在当前版本卸载项。
3. 执行卸载。

预期：

- 卸载完成后当前版本系统卸载项被删除。
- 删除依据是 `install.manifest.json` 中 `actual.systemUninstallEntry[]` 的 `displayName + scope`。

### TC-R02 manifest 可读时清理 uninstaller legacyEntries

准备：

1. 手动创建旧卸载项：
   - key：`MTITest_LegacyUninstallKey`
   - `DisplayName=MyDesktopApp Legacy`
2. 配置：
   ```yaml
   uninstaller:
     cleanup:
       systemUninstallEntry:
         scope: user
         displayName: MyDesktopApp
         legacyEntries:
           - displayName: MyDesktopApp Legacy
             scope: user
   ```

步骤：

1. 安装。
2. 卸载。

预期：

- 当前版本卸载项被删除。
- `DisplayName=MyDesktopApp Legacy` 的旧卸载项被删除。

### TC-R03 卸载不清理 installer legacyEntries

准备：

1. 创建旧卸载项 `DisplayName=Only Installer Legacy`。
2. 只在 `installer.cleanup.systemUninstallEntry.legacyEntries[]` 配置该 displayName。

步骤：

1. 安装。
2. 卸载。

预期：

- `Only Installer Legacy` 不因卸载被清理。

### TC-R04 manifest 缺失 fallback 卸载按 uninstaller cleanup 当前项清理

准备：

1. 安装后删除 `%InstallDir%\install.manifest.json`。
2. 保留 installState detect 可发现安装目录。
3. 配置 `uninstaller.cleanup.systemUninstallEntry.displayName/scope`。

步骤：

1. 执行卸载。

预期：

- fallback 卸载生效。
- 当前版本系统卸载项按配置的 `displayName + scope` 删除。

### TC-R05 manifest 缺失 fallback 卸载清理 uninstaller legacyEntries

准备：

1. 同 TC-R04。
2. 额外创建旧卸载项 `DisplayName=MyDesktopApp Legacy`。
3. 配置 `uninstaller.cleanup.systemUninstallEntry.legacyEntries[]`。

预期：

- 当前版本卸载项被删除。
- legacy 卸载项被删除。

## 8. 精确匹配测试

### TC-P01 只按 DisplayName 匹配，不按 key 匹配

准备：

1. 创建卸载项：
   - key：`MyDesktopApp Legacy`
   - `DisplayName=Different Display`
2. 配置 legacy entry：
   ```yaml
   displayName: MyDesktopApp Legacy
   scope: user
   ```

预期：

- 不删除该项，因为 `DisplayName` 不匹配。

### TC-P02 key 不同但 DisplayName 匹配时删除

准备：

1. 创建卸载项：
   - key：`RandomLegacyKey`
   - `DisplayName=MyDesktopApp Legacy`
2. 配置 legacy entry：
   ```yaml
   displayName: MyDesktopApp Legacy
   scope: user
   ```

预期：

- 删除该项。

### TC-P03 scope=user 不删除 machine 项

准备：

1. 在 HKLM 下创建 `DisplayName=MyDesktopApp Legacy`。
2. 配置：
   ```yaml
   displayName: MyDesktopApp Legacy
   scope: user
   ```

预期：

- HKLM 项不被删除。

### TC-P04 scope=both 删除 user 与 machine 项

准备：

1. HKCU 与 HKLM 下分别创建 `DisplayName=MyDesktopApp Legacy`。
2. 配置：
   ```yaml
   displayName: MyDesktopApp Legacy
   scope: both
   ```

预期：

- 两处卸载项均被删除。

## 9. 通过标准

本轮验收通过需满足：

- 自动化构建与回归测试全部通过。
- 合法 v3 配置可打包，旧字段配置明确失败。
- 覆盖/升级只消费 `installer.cleanup.systemUninstallEntry.legacyEntries[]`。
- 卸载只消费 manifest 实际写入项与 `uninstaller.cleanup.systemUninstallEntry.legacyEntries[]`。
- fallback 卸载按 `uninstaller.cleanup.systemUninstallEntry` 配置清理。
- 所有系统卸载项删除均只依赖 `DisplayName + scope`。
- 文档、示例、测试中不再把 `uninstaller.cleanup.legacy.uninstallEntries` 或字符串型 `systemUninstallEntry: auto` 作为可用配置。
