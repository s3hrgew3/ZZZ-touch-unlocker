# 在 Wine 下使用 zzz-touch-unlocker

这个目录是 **Wine 移植分支** 的产物：一个 POSIX 启动器 + 一个“事后注入”工具，用来在 Linux/Wine
上把 `zzz-touch-hook.dll` 装进已经跑起来的《绝区零》进程。

原生 Windows 的用法（`zzz-touch-unlocker.exe`：挂起创建 → 注入 → 恢复）在 Wine 下**结构上不可用**，
原因是启动顺序，不是代码 bug。下面先说清楚原因，再讲怎么用。

---

## 1. 为什么原生启动器在 Wine 下不行

HoYoKProtect 的驱动加载路径对 **“谁是这台 wineserver 的第一个客户程序”** 极其敏感：

- 只有当**游戏本身是该 prefix 里 wineserver 启动后的第一个 wine 客户程序**时，
  `ZwLoadDriver` 才会真正把保护驱动拉起来（能看到 `winedevice.exe` 被创建、游戏正常进 Unity）。
- 一旦在此之前有**任何** wine 进程连上同一台 wineserver（另一个程序、`wine cmd`、
  甚至只是一个 `wineserver` 客户），驱动就不会被 spawn，游戏随后调用
  `NtRaiseHardError 0x50000018`，而 Wine 把这个 API 实现成 stub ——
  于是**没有窗口、没有报错、约 5 秒后进程直接消失**，Unity 连一个字节的日志都不会写。

已在同族保护（原神 / `HoYoKProtect.sys`）上做过 10 组受控实验确认，其中关键两组：

| 组 | wineserver 状态 | 结果 |
|---|---|---|
| 9 | 清空后冷启动，游戏是第一个客户 | 正常：`winedevice.exe` 出现，DXVK 初始化，进入加载界面 |
| 10 | 先用 `wine cmd /c exit` 占住 server，再启动游戏 | 5 秒后 `NtRaiseHardError 0x50000018` 退出 |

而 `zzz-touch-unlocker.exe` 的工作方式恰好撞在这条规则上：它自己是 wine 客户，
注入动作也由它发起，因此**游戏永远是第二个客户**。这不是它在 Wine 下“跑不起来”，
而是它把启动顺序搞反了。

> 《绝区零》的游戏目录里同样有 `HoYoKProtect.sys`（约 4 MB）和 `mhypbase.dll`，
> 属于同一套保护，因此这里按同一条规则处理。

### 结论：换模型，而不是打补丁

|  | 原生 Windows | Wine（本目录） |
|---|---|---|
| 谁创建进程 | 启动器 `CreateProcessW(CREATE_SUSPENDED)` | 启动器直接用 `wine` 起游戏 |
| 注入时机 | 进程挂起、第一条指令之前 | 游戏跑起来、保护期结束之后 |
| 注入者 | 启动器自身 | `zzz-touch-inject.exe`（第二个客户） |
| 模式传递 | 子进程环境变量 `ZZZ_TOUCH_MODE` | 钩子目录下的 `zzz-touch-mode.txt` |
| 失败表现 | 启动器报错、杀掉挂起进程 | 启动器报“游戏提前退出”、不注入 |

第二种客户在游戏跑起来之后再接入是**安全**的：被测试过的场景里，游戏启动完成后再 attach
第二个 wine 程序，游戏进程不受影响、日志继续正常输出。

---

## 2. 启动顺序模型

```
zzz-touch-unlocker run
   │
   ├─ 1. clean      杀掉该 prefix 的所有 wine 客户 + wineserver -k，确认清零
   │                （这一步等价于原神那边的 ys-clean）
   ├─ 2. mode       写 <dll 目录>/zzz-touch-mode.txt = 0|1
   ├─ 3. launch     cd <game dir> && WINEPREFIX=... wine ZenlessZoneZero.exe   ← 第一个客户
   ├─ 4. wait       等游戏镜像被映射（在 /proc/*/maps 里找 ZenlessZoneZero.exe）
   ├─ 5. guard      等保护期结束，最多 --grace-ms（默认 6000ms）：
   │                   · winedevice.exe 出现（驱动宿主，保护驱动已加载）
   │                   · 或 Player.log 被本次启动更新（Unity 已开始初始化）
   │                   · 或 grace 到点
   └─ 6. attach     wine zzz-touch-inject.exe --name <game> → LoadLibraryW(钩子)  ← 第二个客户
```

第 4 步不能看进程名：runner 的 steam stub 会把 unix 进程名固定成 `steam.exe`，
真正的游戏是被它 fork 出来的子进程，只有镜像映射是可靠的“游戏已加载”信号。
同理，Linux pid ≠ Windows pid，所以注入器用 Toolhelp 按镜像名找目标（`--name`），
`--pid` 只在明确知道 Windows 侧 pid 时才用。

第 5 步是这次移植的核心保护：**注入得太早 = 在保护期里当第二个客户 = 游戏自杀**。
所以先等信号，信号没来就等 grace，然后再注入。

钩子侧不需要改行为，只多了两处适配：

- 模式可以从 `zzz-touch-mode.txt` 读（环境变量仍然优先，Windows 流程完全不变）；
- 日志目录可用 `ZZZ_TOUCH_LOG_DIR` 指定；日志里多打一行 `NTR_HOST wine=<版本> modeSource=ENV|FILE`。

---

## 3. 构建

```bash
sudo apt install mingw-w64 cmake ninja-build     # 一次就够
scripts/build-wine.sh                            # → dist/wine/
```

产物：

| 文件 | 用途 |
|---|---|
| `zzz-touch-hook.dll` | 钩子本体（和 Windows 版同一份源码，静态链接 C++ 运行时） |
| `zzz-touch-inject.exe` | 事后注入器，Wine 下由启动器调用 |
| `zzz-touch-unlocker` | POSIX 启动器（本目录脚本） |
| `zzz-touch-unlocker.exe` | 原生启动器，**故意不放进 `dist/wine`**，避免被误用 |

`--runner`/`--prefix` 默认从 sleepy-launcher 的 `config.json` 读，所以不需要 export 一堆环境变量。

---

## 4. 用法

```bash
# 看检测结果（不改任何东西，建议第一步就跑这个）
dist/wine/zzz-touch-unlocker status

# 正常启动：清 prefix → 起游戏 → 等保护期 → 注入
dist/wine/zzz-touch-unlocker run

# 桌面 UI
dist/wine/zzz-touch-unlocker run -m 0

# 游戏已经在跑（比如从启动器启动的），只补一次注入
dist/wine/zzz-touch-unlocker attach

# 只清 prefix，不开游戏
dist/wine/zzz-touch-unlocker clean

# 交给游戏的参数照常透传
dist/wine/zzz-touch-unlocker run -- -screen-fullscreen 0
```

常用选项：

| 选项 | 说明 |
|---|---|
| `-m, --mode 0\|1` | 0 = 桌面 UI，1 = 手机 UI（默认 1） |
| `--grace-ms N` | 保护期最长等待（默认 6000）。注入晚了导致钩子拿不到早期函数，就调小 |
| `--wait-ms N` | 等待游戏进程出现的最长时间（默认 120000） |
| `--edition china\|global` | 选国服/国际服路径（默认读启动器配置） |
| `--game / --prefix / --runner` | 手工覆盖检测结果 |
| `--no-clean` | 不杀已有 wine 客户（**除非你确定 prefix 是干净的**） |
| `--no-inject` | 只起游戏，不注入（对比实验用） |
| `--dry-run` | 只打印将执行的命令 |
| `--env K=V` | 额外传给游戏的环境变量（可重复，如 `--env MANGOHUD=1`） |

退出码：`0` 成功，`1` 参数/预检失败，`2` 游戏提前退出（多半是保护期被杀），
`3` 注入失败，`4` prefix 没清干净。

### 和启动器的关系

`run` 会**绕过 sleepy-launcher** 直接 `wine ZenlessZoneZero.exe`。这会丢掉启动器附加的东西
（MangoHUD、gamemode、`game.environment`），DXVK 不受影响（DLL 装在 prefix 里）。
想要启动器那些增强，可以：

```bash
dist/wine/zzz-touch-unlocker clean            # 先把 prefix 清空
sleepy-launcher                               # 在启动器里点启动（游戏仍是第一个客户）
dist/wine/zzz-touch-unlocker attach           # 进游戏后再补注入
```

第二种方式同样安全 —— 前提是**启动器本身那时候不能是 wine 客户**。启动器是原生 GTK/Qt 程序，
不占 wineserver，所以这个组合是可用的。

---

## 5. 日志与验证

- 钩子日志：`<dll 目录>/logs/touch-YYYYMMDD-HHMMSS.log`（可用 `--log-dir` 改）
- 游戏 stdout/stderr：`<log dir>/game.log`（可用 `--game-log` 改）

一次成功的启动应该看到：

```
game pid 12345 is up (0s)
anti-cheat stage cleared after 2345ms (signal: winedevice)
injecting hook into pid 12345
  zzz-touch-inject (wine=11.19)
    dll    = Z:\...\zzz-touch-hook.dll
    mode   = 1 (MOBILE) -> Z:\...\zzz-touch-mode.txt
    target = ZenlessZoneZero.exe (pid=12345)
    injected OK
hook log: .../logs/touch-....log
hook result: NTR_READY
```

钩子日志里应当依次出现：

```
NTR_HOST wine=11.19 modeSource=FILE dllDir=Z:\...
NTR_MODE mode=1 target=MOBILE liveLayoutPlatform=1
LTR_INPUT_LOCATED ... / UIR_LAYOUT_LOCATED ... / UIR_LAYOUT_OVERRIDE_RESULT state=PATCHED
NTR_READY touchInput=LEGACY_NATIVE liveLayoutPlatform=1 ...
```

没有 `NTR_READY` 就是这个分支最需要关注的信息，见下一节。

---

## 6. 故障排查

**点完启动，5 秒内没有窗口就退出了**
保护期里出现了第二个 wine 客户。按顺序查：

```bash
pgrep -af 'wineserver|winedevice'            # 有输出说明 prefix 是“热”的
dist/wine/zzz-touch-unlocker status          # prefix clients 必须是 none
```

`--no-clean` 是个危险开关：用它之前先 `status` 确认 prefix 干净。

**`injection failed` / `target process not found`**
国际服/测试服的镜像名不是 `ZenlessZoneZero.exe` 时，用 `--name` 指向真实镜像名：

```bash
grep -l ZenlessZone /proc/*/maps            # 确认镜像名
dist/wine/zzz-touch-unlocker attach --name ZenlessZoneZero.exe
```

`--pid` 接受的是 **Windows 侧 pid**（注入器打印的 `target = ... (pid=...)`），
不是 `pgrep` 得到的 Linux pid。

**钩子写了 `NTR_ABORT ... missing/invalid launcher mode`**
`zzz-touch-mode.txt` 没写进去。检查 `--dll` 指向的目录是否可写；
注入器打印的 `mode ->` 行会显示它写在哪。

**钩子写了 `NTR_ABORT layout finalize failed`**
60 秒内没找到 layout 特征。定位预算是 `ZZZ_TOUCH_FINALIZE_TIMEOUT_MS`（默认 60000，测试里会调小）。
游戏版本不匹配或注入到了一个空白宿主上时会这样。

**钩子写了 `NTR_ABORT early override arm failed`**
注入太晚，Unity 已经初始化完，早期钩子没挂上。把 `--grace-ms` 调小（比如 `2000`）重试。
如果一直失败，说明这台机器的保护期本来就长，需要看 `signal:` 那一行是什么触发的。

**注入成功但界面没变**
`NTR_READY` 里有 `liveLayoutPlatform` 和 `layoutFieldOffset`，对照原生分支的日志确认字段偏移没被新版本改掉；
模式文件是否真的是 `1`；以及确认游戏进程是这次新起的（老的 `Player.log` 会误导判断）。

**`prefix still has wine clients`**
有进程拒绝退出（比如另一个 prefix 的 `wineserver` 被错误匹配 —— 本脚本按 `WINEPREFIX` 精确匹配，
不会误杀）。用 `--pid` 手工 `kill`，或者重启一次桌面会话。

---

## 7. 已知限制

- **Wine 侧的实机验证状态**：注入链路（`wineserver` 清理、进程发现、`LoadLibraryW`、
  钩子自举、模式文件、日志）在本机 Wine 上用合成目标验证过；
  真机《绝区零》一次完整启动仍需在游戏本体上跑一遍（`status` → `run` → 看 `NTR_READY`）。
- **`--grace-ms` 是经验值**：默认 6 秒。保护期长度随游戏版本/wine 版本变化，必要时调。
- **不改 prefix**：本分支不碰 DXVK、不改 `config.ini`、不写注册表、不动游戏文件；
  唯一写入 prefix 之外的是 `<dll 目录>/zzz-touch-mode.txt` 和日志目录。
- **原生 Windows 流程零变化**：`zzz-touch-unlocker.exe` 只是把注入实现挪进了 `launcher/inject.cpp`，
  并在检测到 Wine 时直接拒绝运行（给出这条规则的提示），避免有人误用后以为“注入成功但游戏闪退”。

---

## 8. 相关文件

| 路径 | 作用 |
|---|---|
| `launcher/inject.{h,cpp}` | 进程查找、Wine 检测、`LoadLibraryW` 注入、模式文件读写 |
| `launcher/inject_main.cpp` | `zzz-touch-inject.exe` 命令行入口 |
| `launcher/main.cpp` | 原生启动器（新增 Wine 拒绝分支） |
| `hook/touch_hook.cpp` | 模式来源：环境变量 → 模式文件；`NTR_HOST` 日志 |
| `wine/zzz-touch-unlocker` | POSIX 启动器 |
| `scripts/build-wine.sh` | MinGW 交叉构建 + 暂存到 `dist/wine` |
| `cmake/mingw-w64-x86_64.cmake` | 交叉工具链文件 |
| `wine/zzz-touch-selftest.sh` | 端到端冒烟测试（合成目标，不需要游戏） |
| `wine/touch-probe.c` | `WM_TOUCH` 能力探针（判断某个 runner 能不能送触摸） |
| `wine/uinput-tap.c` | 经 `/dev/uinput` 合成一次真实触摸（自检用，不需要人动手） |
| `scripts/check-wine-touch.sh` | 一键跑探针：`--list` 列 runner，给 runner 出 PASS/FAIL |

## 9. Wine runner 与触摸能力（实测）

**触摸能不能用，取决于 runner 的 `win32u` 有没有实现触摸窗口 API，和本项目无关。**

上游 Wine 的 `NtUserIsTouchWindow` 是 `@ stub -syscall`，`WM_TOUCH` 只有宏定义没有发送方；
部分下游构建（CachyOS、dwproton 系）补了这套实现，TkG staging 没有。本机实测：

| runner | `WM_TOUCH` | 结论 |
|---|---|---|
| `spritz-wine-cachyos-wow64-10.0-11` | 送达（`IsTouchWindow -> 1`，一次 300 ms 触摸收到 ~20+ 条 `WM_TOUCH`） | ✅ 用这个 |
| `spritz-wine-tkg-staging-wow64-11.19-1` | 不送达（`RegisterTouchWindow` fixme stub，`NtUserIsTouchWindow` 未实现会直接崩进程） | ❌ 别用 |
| `wine-dwproton` / `wine-staging-tkg` / `soda` | 启动器里可装，本机未装、未实测 | 装后先跑探针 |

自己验一遍：

```bash
dist/wine/check-wine-touch.sh --list             # 看启动器里可装的 runner
dist/wine/check-wine-touch.sh                    # 测当前选中的 runner
dist/wine/check-wine-touch.sh --runner ~/.local/share/sleepy-launcher/runners/<名>
dist/wine/check-wine-touch.sh --manual           # 不想用 uinput 时，自己用手点窗口
```

退出码 0 = 能送触摸，1 = 不能，2 = 用法/环境问题。

在启动器里换 runner：设置 → Wine 版本（`game.wine.selected`），换完重开一次游戏。
**注意**：整条链是 `Sunshine → uinput → Wayland/XWayland → winex11.drv → win32u → WM_TOUCH → Unity`，
只要 `win32u` 这一环是 stub，游戏里 `Input.touchCount` 恒为 0，摇杆就拖不动——
那时候换 runner 是唯一解，hook 层改不了这件事。
