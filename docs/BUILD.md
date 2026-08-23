# Build Guide

## Native plugin

1. Install the Fallout 4/CommonLibF4 development dependencies and the x64
   Visual Studio toolchain
2. Open either `CHS/2.0.5/Native` or `EN/2.0.5/Native`
3. Make the CommonLibF4 and spdlog paths available to the local build setup
4. Build with the repository's `xmake.lua`

The two language builds use the same native API and runtime logic. Choose the
language directory before compiling so the localized native messages match the
selected UI.

## Papyrus

Compile the four files under the selected language's
`Papyrus/Source/User` directory with Caprica or another Fallout 4 Papyrus
compiler. The compiler output belongs in the release package's `Scripts`
directory and is not committed to this source repository.

## UI package assembly

Copy the selected language's `UI` tree into the package root. Copy the mascot
assets from the stable release package into
`PrismaUI_F4/views/OutfitManager/assets` beside `menu.html`. The JSON files
under `UI/F4SE/Plugins/OutfitManager` provide the 16:9 and 16:10 layout
profiles and UI tuning values.
