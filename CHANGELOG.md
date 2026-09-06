# Changelog

## 1.1.1

- Compared with 1.1.0, fixed several bugs encountered during runtime on the AE
  version.

## 1.1.0

- Matched the PrismaUI 2.1 Ultralight runtime contract while retaining the
  V1-V10-compatible public API path.
- Replaced large C++ to JavaScript state, studio-inventory, material, and result
  pushes with JSON `InteropCall` delivery.
- Added per-view JavaScript console logging and unhealthy-view recovery using
  PrismaUI's view-health API.
- Added first-focus warmup and a lightweight open-view liveness pulse for the
  Ultralight CPU compositor.
- Removed the obsolete one-shot paint-pulse workaround, unreachable 1px media
  queries, unused native helpers, and stale migration comments.
- Added Steam Deck-safe font fallbacks and tabular numeric rendering, centered
  generated icons, and a compact quick-outfit baseline that follows layout scaling.
- Added initial-delay/repeat handling for held keyboard navigation and left-stick
  navigation, with release/close cleanup.
- Kept the view reusable across normal closes instead of destroying and
  recreating it through the former CEF clear-frame path.
- Preserved the 1.0.2 source and release packages as the rollback baseline.

## 1.0.2

- Reapply an NPC's active saved outfit after its cell is reattached or fully
  loaded, preventing fast travel from restoring the vanilla outfit.
- Recheck managed NPC outfits after AI/default-outfit equipment events so daily
  schedule changes and long-distance unloading cannot silently replace them.
- Route real NPC outfit items through the teammate container-transfer path and
  preserve the NPC equip-state lock, matching the vanilla trade-menu equip path.
- Delay and retry the reapply on the game thread so NPC inventory and 3D state
  have time to settle after a cell transition.

## 1.0.1

- Remember the last Quick Outfit Switcher page between openings.
- Preserve the page when switching to frequency-based sorting, while reopening
  without a focus so the reordered list cannot select the wrong outfit.
- Store the remembered quick-outfit page in the native session state so it
  survives PrismaUI view destruction and recreation.
- Delay focus placement after mouse-wheel and right-stick scrolling until the
  scroll settles, then focus the first visible selectable item once.
- Prefer a fully visible item for delayed focus, retry after DOM refreshes, and
  increase right-stick scrolling from 22px to 48px per input step.
- Use a 400ms scroll-idle delay so focus recovery feels more responsive while
  avoiding repeated focus traversal during active scrolling.
- Keep delayed focus recovery alive across large mouse-wheel bursts and
  refresh the live list after the scroll has settled.
- Tighten the fixed quick-outfit panel around its ten-row layout so the
  unused space above the shortcuts stays small and consistent in EN and CHS.
- Raise the fixed quick-outfit panel slightly so the tenth Chinese outfit row
  remains fully visible at 16:9 1080p.

## 1.0.0 - NG release line

- Started the Aozora Outfit Management System NG version line.
- Consolidated the post-2.0.4 outfit, studio, material, and input work into a
  clean source baseline.
- Retained internal `OutfitManager` identifiers for existing outfit data and
  settings compatibility.
- Included the stable indoor/outdoor preview-light architecture and separate
  scene brightness scales.
- Restored the required mascot assets to both language source trees.
- Loaded the home mascot independently of menu focus, including on the first
  open and whenever the main page is rendered again.
- Filled the saved-outfit detail pane with the first visible outfit when the
  outfit browser opens without a focus selection.
- Reset the saved-outfit browser selection when changing pages so each page
  starts by displaying its first visible outfit.
- Keep keyboard and gamepad navigation ahead of stationary-pointer hover events
  across content lists and save-slot dialogs.
- Close the slot metadata reader before atomically replacing its JSON file.
