# Build Guide

This document describes the NG `1.1.0` build using the `1.1.0`
language source layout. The repository does not
vendor the large CommonLibF4 dependency or generated Fallout 4 binaries.

## Native plugin

1. Install the Fallout 4/CommonLibF4 development dependencies and the x64
   Visual Studio toolchain.
2. Use the checked-in upstream development baseline at
   `../_upstream/commonlibf4-dear-latest`, or replace the two `1.1.0` xmake
   paths with an equivalent CommonLibF4 tree that declares AE `1.11.240`.
3. Open either `CHS/1.1.0/Native` or `EN/1.1.0/Native`.
4. Build with the repository's `xmake.lua`:

   ```powershell
   xmake build -y
   ```

The two language builds use the same native API and runtime logic. Choose the
language directory before compiling so the localized native messages match the
selected UI.

## Papyrus

Compile the six files under the selected language's
`Papyrus/Source/User` directory with Caprica or another Fallout 4 Papyrus
compiler. The compiler output belongs in the release package's `Scripts`
directory and is not committed to this source repository.

## UI package assembly

Copy the selected language's `UI` tree into the package root. The UI tree
already contains the required mascot assets under
`PrismaUI_F4/views/OutfitManager/assets` beside `menu.html`. The JSON files
under `UI/F4SE/Plugins/OutfitManager` provide the 16:9 and 16:10 layout
profiles and UI tuning values.

The native target includes Old-Gen `1.10.163` and AE `1.11.137` through
`1.11.240`. The 1.1.0 release package targets PrismaUI 2.1.0 or newer. The previous 1.0.2
package remains the rollback baseline for PrismaUI 2.0.6. The release package
must also contain the separately built `OutfitManager.dll`,
compiled Papyrus `.pex` files, and the release `OutfitManager.esp`. Keep EN and
CHS packages separate and install only one language package.
