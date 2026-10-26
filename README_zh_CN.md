# zzz-touch-unlocker

[English](README.md)

为 **Windows 版《绝区零》**恢复原生触摸输入和移动端 UI。

本工具恢复游戏原本的 Windows 原生触摸链路，启用原生移动端 UI、虚拟摇杆和触控操作，通过启动参数选择桌面或移动布局。运行时目标通过语义 Pattern 定位，不依赖固定版本 RVA 表。

已针对 **绝区零 3.2** 完成适配修改：移除新版中失效的保存 hook，改为通过启动参数强制选择布局。已通过离线特征检查、编译、参数检查及用户游戏内验证。旧版移动端实现曾在 **3.1.0** 验证。

已在 **绝区零 3.3** 验证，无需修改

## 下载与使用

从 [Releases](../../releases/latest) 下载最新版本，解压后保持以下两个文件在同一目录：

```text
zzz-touch-unlocker.exe
zzz-touch-hook.dll
```

关闭游戏，然后**以管理员身份运行 `zzz-touch-unlocker.exe`**。

```bat
zzz-touch-unlocker.exe
```

### 模式参数与默认行为

**直接双击或不带参数运行，等同于 `-m 1`：启用移动端布局，并恢复原生触摸输入。**

| 参数 | 作用 | 默认值 |
| --- | --- | --- |
| `-m 0` | 强制桌面布局，保留游戏默认输入后端 | — |
| `-m 1` | 强制移动布局，启用原生触摸输入、虚拟摇杆和触控 UI | 是 |
| `--mode 0` / `--mode 1` | 与 `-m 0` / `-m 1` 等价 | `1` |
| `--game "路径"` | 指定游戏可执行文件 | 见下方默认路径 |
| `--help` / `-h` | 显示使用帮助 | — |

```bat
rem 桌面模式
zzz-touch-unlocker.exe -m 0

rem 移动模式（默认）
zzz-touch-unlocker.exe -m 1

rem 等价的长参数写法
zzz-touch-unlocker.exe --mode 1
```

模式只接受 `0` 或 `1`，缺少值或其他值会报错。切换模式前请先关闭游戏，再使用对应参数重新启动。

如需双击进入桌面模式，可创建启动器的快捷方式，在其“目标”末尾添加 ` -m 0`，例如：

```text
"C:\zzz-touch-unlocker\zzz-touch-unlocker.exe" -m 0
```

### 透传游戏启动参数

除启动器自身的 `-m` / `--mode`、`--game` 和帮助参数外，其余参数按原顺序传给游戏。例如：

```bat
zzz-touch-unlocker.exe -m 0 -screen-width 1920 -screen-height 1080 -screen-fullscreen 0 -monitor 1
```

启动器使用桌面模式，并将后面的分辨率、全屏和显示器参数传给游戏。参数是否生效取决于游戏本身的支持情况。

如需把与启动器同名的参数也传给游戏，可使用 `--` 分隔；`--` 本身不会传递，其后所有参数均传给游戏。启动器参数应放在分隔符之前：

```bat
zzz-touch-unlocker.exe -m 0 -- -screen-width 1920 -screen-height 1080
```

带空格的参数值请用双引号括起来，例如 `-logFile "D:\Game Logs\zzz.log"`。参数值中的空格、引号和反斜杠会在传递时保留。

### 配合绝区零一条龙使用

已适配 [ZenlessZoneZero-OneDragon（绝区零一条龙）](https://github.com/OneDragon-Anything/ZenlessZoneZero-OneDragon) 的启动参数透传：

1. 在一条龙中，将游戏路径选择为 `zzz-touch-unlocker.exe`。
2. 在高级参数中添加 `-m 0`，即可固定按桌面版启动。

一条龙附加的分辨率、全屏和显示器等启动参数会继续传给游戏。请保持 `zzz-touch-hook.dll` 与启动器在同一目录。

如果游戏不在默认安装路径，还需在高级参数中指定实际游戏路径，例如：

```text
-m 0 --game "D:\Games\ZenlessZoneZero Game\ZenlessZoneZero.exe"
```

### 游戏路径

默认游戏路径为：

```text
C:\Program Files\miHoYo Launcher\games\ZenlessZoneZero Game\ZenlessZoneZero.exe
```

如果游戏安装在其他位置：

```bat
zzz-touch-unlocker.exe --game "D:\Games\ZenlessZoneZero Game\ZenlessZoneZero.exe" -m 1
```

启动器会先以 suspended 状态创建游戏进程，加载 hook DLL，完成早期运行时修改后再恢复游戏运行。

## 编译

需要：

- Windows x64
- Visual Studio 2022 或更新版本，并安装“使用 C++ 的桌面开发”
- CMake

直接运行：

```bat
scripts\build.bat
```

脚本配置 x64 Release 构建，完成编译并将运行文件复制到：

```text
dist\zzz-touch-unlocker.exe
dist\zzz-touch-hook.dll
```

等价的手动编译方式：

```bat
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

仓库也配置了 GitHub Actions 自动编译。推送 `v0.1.0` 这类 tag 后，会自动创建 GitHub Release 并上传 Windows x64 压缩包。

## Linux / Wine

Windows 版启动器无法在 Wine 下工作：它会先挂起子进程再注入，这个子进程就成了该 prefix 的**第二个** wine 客户端，反作弊驱动拿不到首个客户端的初始化握手，游戏会在约 5 秒后静默退出。

因此 Linux 下改由 `wine/zzz-touch-unlocker` 掌控启动顺序：

```bash
./scripts/build-wine.sh                 # 用 MinGW 交叉编译到 dist/wine/
dist/wine/zzz-touch-unlocker run -m 1   # 清空 prefix -> 启动游戏 -> 附着 hook
```

它先清空 prefix（游戏必须是首个 wine 客户端），用一条 wine 命令启动游戏，等反作弊阶段结束后再用 `zzz-touch-inject.exe` 载入 hook。模式通过 `zzz-touch-mode.txt` 传给 hook，附着过程不需要导入环境变量。另有 `attach`、`clean`、`status` 三个子命令。

配置默认读取 sleepy-launcher 的 `config.json`，游戏路径、prefix、runner 一般不需要额外参数。日志含义与排障见 [wine/README.md](wine/README.md)；`wine/zzz-touch-selftest.sh` 用合成窗口目标跑通全流程，不需要游戏本体。

## 实现原理

运行时进行两项核心操作。

### 1. 恢复原生触摸输入

仅在移动模式下修改输入后端；桌面模式保留游戏默认输入行为。

绝区零同时启用了 Unity 的 New 和 Legacy Windows 输入后端。对于原生 `WM_TOUCH`，New 后端会先消费触摸句柄，使 Legacy 后端无法继续处理同一个触摸输入。

Hook 会定位 Unity 的输入管理器初始化逻辑，并将其切换为等价于：

```text
enableNativePlatformBackendsForNewInputSystem = 0
disableOldInputManagerSupport                 = 0
```

这样只保留 Legacy WinInputManager，从而恢复：

```text
WM_TOUCH
  -> Unity Legacy WinInputManager
  -> Input.touchCount / Input.GetTouch
  -> 绝区零原生触摸逻辑
```

### 2. 按参数强制布局

通过 Pattern 定位 `LocalUILayoutPlatform` 读取逻辑，在运行时覆盖字段和返回值：

| 参数 | 游戏内部布局值 | 布局 |
| --- | --- | --- |
| `-m 0` | `2` | 桌面 |
| `-m 1`（默认） | `1` | 移动、虚拟摇杆和触控 UI |

每次执行该读取路径都会采用本次选择，不依赖已保存的布局。仍使用内存补丁，但不再拦截序列化或保存流程，也不再依赖 MinHook。

游戏自行保存设置，可能保存本次布局；下次通过启动器启动时仍以 `-m` 为准。直接启动游戏时，已保存的布局仍可能影响游戏。

## 版本兼容

运行时地址和字段偏移通过语义 Pattern 扫描和结构校验动态解析。因此，如果新版本只是代码地址发生移动，通常有机会直接继续兼容；如果编译结果、控制流或相关结构发生较大变化，则可能需要更新 Pattern。

离线检查新版本：

```bat
python scripts\audit_compat_patterns.py <UnityPlayer.dll> <GameAssembly.dll>
```

详细说明见 [docs/version-compatibility.md](docs/version-compatibility.md)。

## 参考项目

- [winTEuser/Genshin_StarRail_fps_unlocker](https://github.com/winTEuser/Genshin_StarRail_fps_unlocker)
- [fffonion/ZZZConfigManager](https://github.com/fffonion/ZZZConfigManager)

## 免责声明

本项目为非官方第三方项目，与 HoYoverse / miHoYo 无关。游戏更新可能影响兼容性，请自行评估使用风险。使用本工具造成的任何问题或损失，开发者不承担责任。
