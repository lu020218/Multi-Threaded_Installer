# Multi-Threaded Installer

涓€涓熀浜?C++17 鐨勬墦鍖呬笌瀹夎绯荤粺锛屽寘鍚袱涓牳蹇冪▼搴忥細

- `packager`锛氬皢杈撳叆鐩綍鎵撳寘涓哄畨瑁呭寘锛堟敮鎸?`lzma` / `zstd`锛?- `installer`锛氭墽琛屽畨瑁?闈欓粯瀹夎/鍗歌浇锛屾敮鎸佸绾跨▼瑙ｅ帇涓庣粍浠堕€夋嫨

## 褰撳墠鍘嬬缉绠楁硶鏀寔鐘舵€?
- 宸叉敮鎸侊細`LZMA`锛堝師鏈変富璺緞锛?- 宸叉敮鎸侊細`ZSTD`锛堟墦鍖呯鍘嬬缉 + 瀹夎绔В鍘嬶級
- 閰嶇疆鏂瑰紡锛氬彲鍦?`packager.yaml` 鎴?`packager.json` 涓寚瀹?  - `compressionAlgorithm`: `lzma` 鎴?`zstd`
  - `compressionLevel`: 绾у埆鍙厤缃?
绾у埆鑼冨洿锛?- `lzma`: `0-9`锛堥粯璁?`9`锛?- `zstd`: `1-22`锛堥粯璁?`3`锛?- `-1` 琛ㄧず鈥滄湭鏄惧紡璁剧疆鈥濓紝杩愯鏃舵寜绠楁硶榛樿鍊煎鐞?
## 浠撳簱缁撴瀯

```text
.
鈹溾攢 include/
鈹溾攢 src/
鈹? 鈹溾攢 packager/
鈹? 鈹溾攢 installer/
鈹? 鈹溾攢 gui/
鈹? 鈹斺攢 common/
鈹溾攢 resources/
鈹溾攢 docs/
鈹溾攢 tests/
鈹溾攢 third_party/
鈹斺攢 CMakeLists.txt
```

## 鏋勫缓鍓嶅噯澶?
### 1) 鍚屾瀛愭ā鍧?
```powershell
git submodule update --init --recursive
```

### 2) 绗笁鏂逛緷璧?
榛樿浣跨敤浠撳簱鍐呬緷璧栵細
- `third_party/xz`锛圠ZMA锛?- `third_party/yaml-cpp`锛堥厤缃В鏋愶級
- `third_party/DuiLib_Ultimate`锛圙UI 妯″紡锛?- `third_party/zstd`锛堝彲閫夛紝鍚敤 ZSTD锛?
## 鏋勫缓锛圵indows / MSVC锛?
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_GUI=ON `
  -DSTATIC_LINK_RUNTIME=ON `
  -DENABLE_ZSTD=ON

cmake --build build --config Release
```

浜х墿锛?- `build/Release/packager.exe`
- `build/Release/installer.exe`

## ZSTD锛圡SVC 闈欐€佸簱锛夎鏄?
椤圭洰浼氫紭鍏堣嚜鍔ㄦ娴嬫湰鍦伴潤鎬佸簱锛?- `third_party/zstd/lib_static/zstd_static.lib`

骞惰嚜鍔ㄦ煡鎵惧ご鏂囦欢鐩綍锛堜紭鍏堬級锛?- `third_party/zstd/include`
- `third_party/zstd/lib`

濡傛灉妫€娴嬫垚鍔燂紝浼氳嚜鍔ㄥ惎鐢?`ZSTD_FOUND`锛屼笉闇€瑕佸啀鎵嬪姩鎸囧畾 `ZSTD_FORCE_THIRD_PARTY_BINARY=ON`銆?
## 浣跨敤鏂瑰紡

### 1) 鐢熸垚瀹夎鍖?
```powershell
.\build\Release\packager.exe <input_directory> <output_installer.exe>
```

绀轰緥锛?
```powershell
# 浣跨敤 LZMA
.\build\Release\packager.exe -a lzma -l 9 .\test_input .\dist\MyAppSetup.exe

# 浣跨敤 ZSTD
.\build\Release\packager.exe -a zstd -l 3 .\test_input .\dist\MyAppSetup.exe
```

### 2) 杩愯瀹夎

```powershell
.\dist\MyAppSetup.exe
```

闈欓粯瀹夎锛?
```powershell
.\dist\MyAppSetup.exe -s
```

鍗歌浇锛?
```powershell
.\dist\MyAppSetup.exe --uninstall
```

## 閰嶇疆鏂囦欢绀轰緥锛圷AML锛?
```yaml
Version: "1.0"
AppName: "MyDesktopApp"
InstallDir: "%ProgramFiles%"
compressionAlgorithm: "zstd"
compressionLevel: 3

Folder:
  InstallDir: "bin"
  Roaming: "plugins"
  Local: "userdata"

AutoStartup: false
DesktopIcons: true
RequireAdmin: true
```

瀹屾暣瀛楁鍙傝€冿細`docs/configuration_reference.md`

## 鍛戒护琛屽弬鏁帮紙鎽樿锛?
### packager

- `-a, --algorithm <lzma|zstd>`
- `-l, --level <level>`锛坄lzma: 0-9`, `zstd: 1-22`锛?- `-p, --data-out <file>`
- `-t, --threads <count>`
- `-v, --verbose`
- `-h, --help`

鍙傛暟涓庨厤缃枃浠跺叧绯伙細
- 鎺ㄨ崘鎶?`compressionAlgorithm` / `compressionLevel` 鍥哄寲鍦?`packager.yaml/json`锛堜富閰嶇疆婧愶級銆?- `--algorithm` / `--level` 鐢ㄤ簬涓€娆℃€ц鐩栵紙渚嬪 CI 涓存椂鍒囨崲鎴栧仛鍘嬬缉瀵规瘮锛夈€?- 浼樺厛绾э細`CLI 鍙傛暟` > `閰嶇疆鏂囦欢` > `鍐呯疆榛樿鍊糮銆?
### installer

- `-d, --destination <dir>`
- `-p, --data-package <file>`
- `-t, --threads <count>`
- `-f, --force`
- `-s, --silent`
- `--uninstall`
- `--component <id>` / `--components <a,b,c>` / `--all-components`
- `-v, --verbose`
- `-h, --help`

## 娴嬭瘯

```powershell
cmake -S . -B build-tests -DBUILD_TESTS=ON
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

## 鐩稿叧鏂囨。

- `docs/configuration_reference.md`
- `docs/COMMAND_LINE_REFERENCE.md`
- `docs/BUILD_AND_DEPLOYMENT.md`
- `docs/components_troubleshooting_guide.md`
- `docs/TROUBLESHOOTING.md`
