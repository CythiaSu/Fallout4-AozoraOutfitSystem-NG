# Fallout 4 Aozora Outfit System NG

![Stable](https://img.shields.io/badge/status-stable-2ea043)
![Version](https://img.shields.io/badge/version-NG%201.0.0-58a6ff)
![Languages](https://img.shields.io/badge/languages-CHS%20%7C%20EN-f0c36a)
![Input](https://img.shields.io/badge/input-keyboard%20%7C%20mouse%20%7C%20gamepad-8957e5)

## 简介 | Introduction

**青空服装管理系统 NG** 是 Fallout 4 Aozora Outfit System 的次世代版本。
它在原有版本基础上进行了大量更新与重构，当前正式版本为 **NG 1.0.0**。

**Aozora Outfit System NG** is the next-generation version of Fallout 4 Aozora
Outfit System. It contains a large set of updates and refactors over the
previous version. The current stable release is **NG 1.0.0**.

项目对外名称升级为 NG，但为了保留已有套装数据和配置的兼容性，游戏内的
插件、脚本、MCM 和数据目录标识仍保持为 `OutfitManager`。

The public product name is now NG. For compatibility with existing outfit data
and settings, the in-game plugin, scripts, MCM identifiers, and data directory
still use `OutfitManager`.

## 核心功能 | Highlights

| 功能 | 中文 | English |
| --- | --- | --- |
| 套装管理 | 保存、命名、清除、还原和切换最多 500 套服装 | Save, rename, clear, restore, and switch up to 500 outfits |
| 工作台 | 直接管理当前穿着的服装、装备/卸下物品和材质 | Manage the current worn outfit, equip or unequip items, and edit materials |
| 材质交换 | 预览材质并保存带有独立名称的材质版本 | Preview materials and save named material variants |
| 目标 | 支持玩家和符合条件的人形 NPC | Supports the player and eligible humanoid NPCs |
| 输入 | 键盘、鼠标和手柄；快捷换装与套装管理保留分页 | Keyboard, mouse, and gamepad; paging remains available where needed |
| 焦点 | 键盘输入会接管鼠标焦点，鼠标移动后才回到鼠标焦点 | Keyboard input takes over from the mouse; mouse movement returns focus to the cursor |
| 性能 | 减少重复渲染、无效刷新和不必要的资源复制 | Reduced redundant rendering, invalid redraws, and unnecessary asset duplication |
| 光照 | 重做预览光照，室内/室外分别由 MCM 调整 | Rebuilt preview lighting with separate indoor/outdoor MCM controls |
| 布局 | 16:9 与 16:10 布局；1080p 下优先完整显示 10 项，小屏保留滚动 | 16:9 and 16:10 layouts; 1080p prioritizes 10 visible entries, while smaller screens keep scrolling |

## 次世代版本更新 | NG Updates

### 1. AE 1.11.240 支持 | AE 1.11.240 Support

NG 的原生插件已经按 Fallout 4 AE **1.11.240** 进行支持声明和构建适配。
运行时必须同时使用与当前游戏版本匹配的 F4SE、Address Library、Prisma UI、
MCM 和 GOE；不能混用 1.10.x 与 1.11.x 的前置文件。

The NG native plugin declares and builds for Fallout 4 AE **1.11.240**.
Every prerequisite must match the installed game runtime: F4SE, Address
Library, Prisma UI, MCM, and GOE. Do not mix 1.10.x and 1.11.x prerequisites.

### 2. 更简单的操作与鼠标支持 | Simpler Controls and Mouse Support

界面操作经过重新整理，键盘、鼠标和手柄使用同一套焦点状态逻辑。鼠标悬停
可以建立焦点；之后键盘或手柄导航会接管焦点，只有鼠标再次移动到界面区域时
才返回鼠标焦点，避免两个输入源同时制造两个选中项。

The interface uses one focus model for keyboard, mouse, and gamepad input. A
mouse hover can establish focus; keyboard or gamepad navigation then takes over,
and focus returns to the mouse only after the cursor moves over the interface.
This prevents two input sources from creating competing selections.

### 3. UI 性能优化 | UI Performance

NG 对 Prisma UI 界面进行了性能整理，包括减少重复 DOM 更新、限制无效刷新、
复用布局和资源、对异步状态更新进行保护，并在列表较长时保持必要的滚动能力。

NG improves the Prisma UI layer by reducing redundant DOM updates, avoiding
invalid redraws, reusing layout and assets, guarding asynchronous state
updates, and preserving scrolling only where a list actually needs it.

### 4. 光照重做与 MCM 调节 | Rebuilt Lighting with MCM Controls

角色预览使用重新整理的临时世界光照。室内和室外光照分开配置，并在 MCM 中
提供可调的预览强度；预览结束后会清理临时光照引用，避免影响正常游戏场景。

Character previews use a rebuilt temporary world-lighting setup. Indoor and
outdoor preview strength are configured separately in MCM, and temporary light
references are cleaned up after preview use so normal world lighting is not
affected.

## 兼容性 | Compatibility

### 必需前置 | Requirements

- Fallout 4 AE 1.11.240（或与你的游戏运行时对应的 NG 支持版本）
- 与游戏版本匹配的 F4SE
- 与游戏版本匹配的 Address Library for F4SE Plugins
- 与当前运行时匹配的 Prisma UI Framework
- Mod Configuration Menu (MCM)
- Garden of Eden Papyrus Script Extender (GOE)

- Fallout 4 AE 1.11.240, or another NG-supported runtime matching your game
- F4SE matching the installed runtime
- Address Library for F4SE Plugins matching the installed runtime
- Prisma UI Framework matching the installed runtime
- Mod Configuration Menu (MCM)
- Garden of Eden Papyrus Script Extender (GOE)

请只安装一个语言版本。CHS 和 EN 的程序结构一致，区别主要在 UI、MCM 和
Papyrus 文本；不要同时启用两个语言包，也不要与旧版 OutfitManager 并装。

Install only one language package. CHS and EN share the same program structure
and differ mainly in UI, MCM, and Papyrus text. Do not enable both language
packages or install an older OutfitManager package alongside NG.

### 数据标识 | Data Identifiers

为了保留已有数据兼容性，以下标识保持不变：

The following identifiers remain unchanged for data compatibility:

- Plugin: `OutfitManager.esp`
- Native plugin: `OutfitManager.dll`
- Papyrus scripts: `OutfitManager`, `OMNative`, and controller scripts
- MCM identifiers and settings paths: `OutfitManager`
- Shared outfit data directory: `F4SE/Plugins/OutfitManager`

## 安装 | Installation

1. 安装全部对应当前 Fallout 4 运行时的前置。
2. 在 MO2 中安装 `Aozora_Outfit_Management_System_NG_1.0.0_CHS.7z` 或
   `Aozora_Outfit_Management_System_NG_1.0.0_EN.7z`。
3. 只选择一个语言版本，并确认它位于相关前置之后。
4. 启动游戏后先确认 MCM 已加载，再使用快捷键或工作台入口。

1. Install all prerequisites matching the current Fallout 4 runtime.
2. Install either `Aozora_Outfit_Management_System_NG_1.0.0_CHS.7z` or
   `Aozora_Outfit_Management_System_NG_1.0.0_EN.7z` through MO2.
3. Choose one language package and place it after the required frameworks.
4. Start the game, confirm that MCM has loaded, then use the hotkeys or
   workbench entry points.

## 仓库结构 | Repository Layout

```text
EN/1.0.0/      English native, Papyrus, UI, and MCM source
CHS/1.0.0/     Simplified Chinese native, Papyrus, UI, and MCM source
docs/           Build notes, release scope, and README screenshots
tools/          Source validation and asset-processing utilities
```

源码仓库遵循原项目的发布边界，不提交编译生成的 DLL、PEX、ESP、构建缓存和
日志。可直接发布给玩家的 CHS/EN 安装包作为独立 Release 产物维护。

Following the original project boundary, this source repository does not track
generated DLL, PEX, ESP, build caches, or logs. The ready-to-install CHS/EN
archives are maintained as separate release artifacts.

## 构建 | Build

原生插件使用 x64 Visual Studio 工具链与 CommonLibF4 构建；进入对应语言的
`Native` 目录后执行：

The native plugin uses the x64 Visual Studio toolchain and CommonLibF4. From the
selected language's `Native` directory, run:

```powershell
xmake build -y
```

Papyrus 源码位于 `Papyrus/Source/User`，使用 Caprica 或兼容的 Fallout 4
Papyrus 编译器编译。完整的源码、构建和打包说明见
[docs/BUILD.md](docs/BUILD.md)。

Papyrus sources are under `Papyrus/Source/User`; compile them with Caprica or
another compatible Fallout 4 Papyrus compiler. See [docs/BUILD.md](docs/BUILD.md)
for the complete source, build, and packaging workflow.

## 界面预览 | Screenshots

### 主界面 | Main Menu

![Main Menu](docs/images/main-menu.png)

### 套装管理 | Manage Outfits

![Manage Outfits](docs/images/manage-outfits.png)

### 材质交换 | Material Swap

![Material Swap](docs/images/material-swap.png)

### MCM 设置 | MCM Settings

![MCM](docs/images/mcm.png)

### 工作台 | Outfit Studio

![Outfit Studio](docs/images/outfit-studio.png)

### 快速换装 | Quick Outfit Switcher

![Quick Outfit Switcher](docs/images/quick-outfit-switcher.png)

### 目标选择 | Select Target

![Select Target](docs/images/select-target.png)

## 许可证 | License

本项目使用 [Aozora Outfit Management System NG Non-Commercial License](LICENSE.md)。
该许可证允许个人非商业使用、修改和再发布，但禁止未经许可的商业使用。

This project is released under the [Aozora Outfit Management System NG
Non-Commercial License](LICENSE.md). Personal non-commercial use, modification,
and redistribution are allowed; commercial use requires permission.
