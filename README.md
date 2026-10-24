# zzz-touch-unlocker

[简体中文](README_zh_CN.md)

Native touch and mobile UI support for **Zenless Zone Zero on Windows**.

The tool restores the game's native Windows touch path, enables the native mobile UI and touch controls, and selects the desktop or mobile layout from a launch option. Runtime targets are located with semantic patterns instead of fixed version RVAs.

Updated for **Zenless Zone Zero 3.2**: the incompatible save hook has been removed, and a launch option now forces the selected layout. Offline pattern checks, compilation, and argument checks have passed, and in-game behavior has been verified by the user. The earlier mobile implementation was verified on **3.1.0**.

Updated for **Zenless Zone Zero 3.3**: Verified, still works, no changes needed.

## Download and use

Download the latest package from [Releases](../../releases/latest), extract it, and keep these two files together:

```text
zzz-touch-unlocker.exe
zzz-touch-hook.dll
```

Close the game, then **run `zzz-touch-unlocker.exe` as administrator**.

```bat
zzz-touch-unlocker.exe
```

### Mode options and defaults

**Double-clicking the launcher or running without arguments is equivalent to `-m 1`: mobile layout with native touch input restored.**

| Option | Behavior | Default |
| --- | --- | --- |
| `-m 0` | Force desktop layout and preserve the game's default input backend | — |
| `-m 1` | Force mobile layout with native touch input, virtual joystick and touch UI | Yes |
| `--mode 0` / `--mode 1` | Equivalent to `-m 0` / `-m 1` | `1` |
| `--game "path"` | Specify the game executable | See the default path below |
| `--help` / `-h` | Show usage help | — |

```bat
rem Desktop mode
zzz-touch-unlocker.exe -m 0

rem Mobile mode (default)
zzz-touch-unlocker.exe -m 1

rem Equivalent long option
zzz-touch-unlocker.exe --mode 1
```

The mode accepts only `0` or `1`. A missing or invalid value produces an error. Close the game before restarting with a different mode.

To enter desktop mode by double-clicking, create a launcher shortcut and append ` -m 0` to its Target, for example:

```text
"C:\zzz-touch-unlocker\zzz-touch-unlocker.exe" -m 0
```

### Forward game launch arguments

Arguments other than the launcher's own `-m` / `--mode`, `--game`, and help options are forwarded to the game in order. For example:

```bat
zzz-touch-unlocker.exe -m 0 -screen-width 1920 -screen-height 1080 -screen-fullscreen 0 -monitor 1
```

The launcher selects desktop mode and passes the resolution, fullscreen, and monitor options to the game. Whether these options take effect depends on the game's support.

Use `--` to forward all remaining arguments, including names that overlap with launcher options. The separator itself is not forwarded. Place launcher options before it:

```bat
zzz-touch-unlocker.exe -m 0 -- -screen-width 1920 -screen-height 1080
```

Quote values containing spaces, for example `-logFile "D:\Game Logs\zzz.log"`. Spaces, quotes, and backslashes within argument values are preserved when forwarding.

### Use with ZenlessZoneZero-OneDragon

Launch argument forwarding supports [ZenlessZoneZero-OneDragon](https://github.com/OneDragon-Anything/ZenlessZoneZero-OneDragon):

1. In OneDragon, select `zzz-touch-unlocker.exe` as the game path.
2. Add `-m 0` to the advanced arguments to always launch in desktop mode.

Resolution, fullscreen, monitor, and other launch arguments added by OneDragon are forwarded to the game. Keep `zzz-touch-hook.dll` in the same directory as the launcher.

If the game is not installed at the default path, also specify its actual executable in the advanced arguments, for example:

```text
-m 0 --game "D:\Games\ZenlessZoneZero Game\ZenlessZoneZero.exe"
```

### Game path

The default game path is:

```text
C:\Program Files\miHoYo Launcher\games\ZenlessZoneZero Game\ZenlessZoneZero.exe
```

For another installation path:

```bat
zzz-touch-unlocker.exe --game "D:\Games\ZenlessZoneZero Game\ZenlessZoneZero.exe" -m 1
```

The launcher starts the game suspended, loads the hook DLL, applies the early runtime changes, and then resumes the game.

## Build

Requirements:

- Windows x64
- Visual Studio 2022 or newer with Desktop development with C++
- CMake

Run:

```bat
scripts\build.bat
```

The script configures an x64 Release build, builds the project, and copies the runtime files to:

```text
dist\zzz-touch-unlocker.exe
dist\zzz-touch-hook.dll
```

Equivalent manual build:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

GitHub Actions also builds the project automatically. Pushing a tag such as `v0.1.0` creates a GitHub Release with the Windows x64 package.

## How it works

The runtime performs two operations.

### 1. Restore native touch input

Only mobile mode changes the input backend; desktop mode preserves the game's default input behavior.

ZZZ enables both Unity's New and Legacy Windows input backends. For native `WM_TOUCH`, the New backend consumes the touch handle before the Legacy backend can use it.

The hook locates Unity's input-manager initialization logic and switches it to the equivalent of:

```text
enableNativePlatformBackendsForNewInputSystem = 0
disableOldInputManagerSupport                 = 0
```

This leaves the Legacy WinInputManager active, restoring the native path:

```text
WM_TOUCH
  -> Unity Legacy WinInputManager
  -> Input.touchCount / Input.GetTouch
  -> ZZZ touch handling
```

### 2. Force the selected layout

A pattern locates the effective `LocalUILayoutPlatform` access. The runtime patch overrides both the field and the return value:

| Option | Internal game value | Layout |
| --- | --- | --- |
| `-m 0` | `2` | Desktop |
| `-m 1` (default) | `1` | Mobile, virtual joystick and touch UI |

Each execution of this access uses the selected mode independently of the saved layout. Runtime memory patches remain, but serialization and saving are no longer intercepted, and MinHook is no longer required.

The game saves normally and may persist the selected layout. On the next launch through this launcher, `-m` takes precedence again. Launching the game directly may still use the saved layout.

## Compatibility

Runtime addresses and field offsets are resolved with semantic pattern scanning and structural validation. A game update that only relocates code may therefore continue to work without source changes, while larger compiler or logic changes may require pattern updates.

For offline compatibility checks:

```bat
python scripts\audit_compat_patterns.py <UnityPlayer.dll> <GameAssembly.dll>
```

See [docs/version-compatibility.md](docs/version-compatibility.md) for details.

## References

- [winTEuser/Genshin_StarRail_fps_unlocker](https://github.com/winTEuser/Genshin_StarRail_fps_unlocker)
- [fffonion/ZZZConfigManager](https://github.com/fffonion/ZZZConfigManager)

## Disclaimer

This is an unofficial third-party project and is not affiliated with HoYoverse / miHoYo. Game updates may change compatibility. Use it at your own discretion. The developer assumes no responsibility for any problems or damages caused by using this tool.
