# Changelog

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
