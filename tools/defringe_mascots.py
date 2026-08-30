from __future__ import annotations

import argparse
import shutil
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter


def contaminated_edge_mask(rgba: np.ndarray) -> np.ndarray:
    rgb = rgba[..., :3].astype(np.int16)
    alpha = rgba[..., 3]
    channel_spread = rgb.max(axis=2) - rgb.min(axis=2)
    brightness = rgb.mean(axis=2)
    near_white = (channel_spread < 32) & (brightness > 190)
    transparent_reach = alpha == 0
    edge_ring = np.zeros_like(transparent_reach)
    for _ in range(8):
        expanded = transparent_reach.copy()
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            expanded |= shift(transparent_reach, dy, dx)
        edge_ring |= expanded & (alpha > 0)
        transparent_reach = expanded
    return near_white & (
        (alpha > 0)
        & ((alpha < 224) | edge_ring)
    )


def shift(array: np.ndarray, dy: int, dx: int) -> np.ndarray:
    result = np.zeros_like(array)
    src_y = slice(max(0, -dy), array.shape[0] - max(0, dy))
    src_x = slice(max(0, -dx), array.shape[1] - max(0, dx))
    dst_y = slice(max(0, dy), array.shape[0] - max(0, -dy))
    dst_x = slice(max(0, dx), array.shape[1] - max(0, -dx))
    result[dst_y, dst_x] = array[src_y, src_x]
    return result


def defringe(image: Image.Image) -> tuple[Image.Image, int]:
    rgba = np.asarray(image.convert("RGBA")).copy()
    target = contaminated_edge_mask(rgba)
    remaining = target.copy()
    known = (rgba[..., 3] > 0) & ~target
    rgb = rgba[..., :3].astype(np.float32)

    for _ in range(24):
        if not remaining.any():
            break
        color_sum = np.zeros_like(rgb)
        neighbor_count = np.zeros(known.shape, dtype=np.float32)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            neighbor_known = shift(known, dy, dx)
            color_sum += shift(rgb, dy, dx) * neighbor_known[..., None]
            neighbor_count += neighbor_known
        fill = remaining & (neighbor_count > 0)
        if not fill.any():
            break
        rgb[fill] = color_sum[fill] / neighbor_count[fill, None]
        known[fill] = True
        remaining[fill] = False

    rgba[..., :3] = np.clip(np.rint(rgb), 0, 255).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA"), int(target.sum())


def palette_rgba(image: Image.Image) -> tuple[np.ndarray, object]:
    palette_data = image.getpalette() or []
    palette_rgb = np.zeros((256, 3), dtype=np.uint8)
    available = min(len(palette_data) // 3, 256)
    if available:
        palette_rgb[:available] = np.asarray(palette_data[: available * 3], dtype=np.uint8).reshape(-1, 3)

    alpha = np.full(256, 255, dtype=np.uint8)
    transparency = image.info.get("transparency")
    if isinstance(transparency, bytes):
        alpha[: min(len(transparency), 256)] = np.frombuffer(transparency[:256], dtype=np.uint8)
    elif isinstance(transparency, int) and 0 <= transparency < 256:
        alpha[transparency] = 0
    return np.column_stack((palette_rgb, alpha)), transparency


def repack_with_original_palette(cleaned: Image.Image, original: Image.Image) -> Image.Image:
    rgba = np.asarray(cleaned.convert("RGBA"))
    flat = rgba.reshape(-1, 4)
    unique, inverse = np.unique(flat, axis=0, return_inverse=True)
    palette, transparency = palette_rgba(original)
    mapped = np.empty(len(unique), dtype=np.uint8)

    for alpha_value in np.unique(unique[:, 3]):
        unique_rows = np.flatnonzero(unique[:, 3] == alpha_value)
        candidates = np.flatnonzero(palette[:, 3] == alpha_value)
        if not len(candidates):
            candidates = np.flatnonzero(
                np.abs(palette[:, 3].astype(np.int16) - int(alpha_value)) ==
                np.abs(palette[:, 3].astype(np.int16) - int(alpha_value)).min()
            )
        colors = unique[unique_rows, :3].astype(np.int32)
        candidate_colors = palette[candidates, :3].astype(np.int32)
        distance = ((colors[:, None, :] - candidate_colors[None, :, :]) ** 2).sum(axis=2)
        mapped[unique_rows] = candidates[distance.argmin(axis=1)].astype(np.uint8)

    indexed = Image.fromarray(mapped[inverse].reshape(rgba.shape[:2]), "P")
    indexed.putpalette(original.getpalette())
    if transparency is not None:
        indexed.info["transparency"] = transparency
    return indexed


def composite(image: Image.Image, background: tuple[int, int, int]) -> Image.Image:
    canvas = Image.new("RGBA", image.size, (*background, 255))
    canvas.alpha_composite(image.convert("RGBA"))
    return canvas.convert("RGB")


def white_outline_mask(image: Image.Image, radius: int) -> Image.Image:
    alpha = image.convert("RGBA").getchannel("A")
    silhouette = alpha.point(lambda value: 255 if value > 8 else 0)
    expanded = silhouette.filter(ImageFilter.MaxFilter(radius * 2 + 1))
    return ImageChops.subtract(expanded, silhouette)


def add_white_outline(image: Image.Image, radius: int) -> Image.Image:
    original = image.convert("RGBA")
    outline_alpha = white_outline_mask(original, radius)
    outline = Image.new("RGBA", original.size, (255, 255, 255, 0))
    outline.putalpha(outline_alpha)
    return Image.alpha_composite(outline, original)


def add_indexed_white_outline(image: Image.Image, radius: int) -> Image.Image:
    indexed = image.copy()
    pixels = np.asarray(indexed).copy()
    counts = np.bincount(pixels.reshape(-1), minlength=256)
    palette, transparency = palette_rgba(indexed)

    best_choice: tuple[int, int, int] | None = None
    for victim in np.argsort(counts)[:32]:
        victim = int(victim)
        candidates = np.arange(256) != victim
        delta = palette[candidates].astype(np.int32) - palette[victim].astype(np.int32)
        distance = (
            (delta[:, :3] ** 2).sum(axis=1)
            + (delta[:, 3] ** 2) * 4
        )
        candidate_indices = np.flatnonzero(candidates)
        replacement_offset = int(distance.argmin())
        replacement = int(candidate_indices[replacement_offset])
        score = int(counts[victim]) * (int(distance[replacement_offset]) + 1)
        if best_choice is None or score < best_choice[0]:
            best_choice = (score, victim, replacement)

    assert best_choice is not None
    _, white_index, replacement_index = best_choice
    pixels[pixels == white_index] = replacement_index

    outline = np.asarray(white_outline_mask(indexed, radius))
    pixels[outline > 0] = white_index

    output = Image.fromarray(pixels.astype(np.uint8), "P")
    palette_data = list(indexed.getpalette() or [0] * 768)
    palette_data[white_index * 3 : white_index * 3 + 3] = [255, 255, 255]
    output.putpalette(palette_data)

    if isinstance(transparency, bytes):
        alpha = bytearray(transparency)
        if white_index < len(alpha):
            alpha[white_index] = 255
        output.info["transparency"] = bytes(alpha)
    elif isinstance(transparency, int):
        output.info["transparency"] = transparency
    return output


def scaled_outline_radius(image: Image.Image, base_radius: int) -> int:
    scale = min(image.size) / 1024
    return max(2, int(base_radius * scale + 0.5))


def build_outline_preview(source: Path, output: Path, radius: int) -> None:
    original = Image.open(source).convert("RGBA")
    outlined = add_white_outline(original, radius)
    original_view = composite(original, (6, 28, 18))
    outlined_view = composite(outlined, (6, 28, 18))
    original_view.thumbnail((720, 690), Image.Resampling.LANCZOS)
    outlined_view.thumbnail((720, 690), Image.Resampling.LANCZOS)
    preview = Image.new("RGB", (1480, 750), (4, 18, 12))
    preview.paste(original_view, ((740 - original_view.width) // 2, 60))
    preview.paste(outlined_view, (740 + (740 - outlined_view.width) // 2, 60))
    draw = ImageDraw.Draw(preview)
    draw.text((24, 20), "ORIGINAL", fill=(220, 240, 226))
    draw.text((764, 20), f"WHITE OUTLINE - {radius}px", fill=(140, 240, 170))
    output.parent.mkdir(parents=True, exist_ok=True)
    preview.save(output, optimize=True)


def build_preview(source: Path, output: Path) -> None:
    original = Image.open(source).convert("RGBA")
    cleaned, changed = defringe(original)
    original_view = composite(original, (6, 28, 18))
    cleaned_view = composite(cleaned, (6, 28, 18))

    sample_box = (140, 100, 880, 790)
    original_crop = original_view.crop(sample_box).resize((740, 690), Image.Resampling.LANCZOS)
    cleaned_crop = cleaned_view.crop(sample_box).resize((740, 690), Image.Resampling.LANCZOS)
    preview = Image.new("RGB", (1480, 750), (4, 18, 12))
    preview.paste(original_crop, (0, 60))
    preview.paste(cleaned_crop, (740, 60))
    draw = ImageDraw.Draw(preview)
    draw.text((24, 20), "BEFORE", fill=(220, 240, 226))
    draw.text((764, 20), f"AFTER - {changed} edge pixels", fill=(140, 240, 170))
    output.parent.mkdir(parents=True, exist_ok=True)
    preview.save(output, optimize=True)


def process_tree(root: Path, backup: Path) -> None:
    files = sorted(root.rglob("*.png"))
    for source in files:
        relative = source.relative_to(root)
        backup_path = backup / relative
        backup_path.parent.mkdir(parents=True, exist_ok=True)
        if not backup_path.exists():
            shutil.copy2(source, backup_path)
        original = Image.open(source).convert("RGBA")
        cleaned, changed = defringe(original)
        cleaned.save(source, optimize=True)
        print(f"{relative}: {changed} edge pixels")


def repack_tree(root: Path, backup: Path) -> None:
    files = sorted(root.rglob("*.png"))
    for source in files:
        relative = source.relative_to(root)
        original_path = backup / relative
        with Image.open(source) as cleaned, Image.open(original_path) as original:
            if original.mode == "P":
                output = repack_with_original_palette(cleaned, original)
                output.save(source, optimize=True, transparency=output.info.get("transparency"))
            else:
                cleaned.convert("RGBA").save(source, optimize=True)
        print(f"{relative}: repacked from {original.mode}")


def outline_tree(root: Path, backup: Path, radius: int) -> None:
    for original_path in sorted(backup.rglob("*.png")):
        relative = original_path.relative_to(backup)
        output_path = root / relative
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with Image.open(original_path) as original:
            image_radius = scaled_outline_radius(original, radius)
            # Keep the original pixels intact. Rebuilding indexed PNGs by
            # replacing a palette entry can silently recolor real image data.
            output = add_white_outline(original.convert("RGBA"), image_radius)
            output.save(output_path, format="PNG", optimize=True)
        print(f"{relative}: {image_radius}px white outline from original")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--preview-source", type=Path)
    parser.add_argument("--preview-output", type=Path)
    parser.add_argument("--outline-preview", action="store_true")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--repack", action="store_true")
    parser.add_argument("--outline", action="store_true")
    parser.add_argument("--outline-radius", type=int, default=10)
    parser.add_argument("--backup", type=Path)
    args = parser.parse_args()

    if args.preview_source and args.preview_output:
        if args.outline_preview:
            build_outline_preview(args.preview_source, args.preview_output, args.outline_radius)
        else:
            build_preview(args.preview_source, args.preview_output)
    if args.apply:
        if not args.backup:
            parser.error("--backup is required with --apply")
        process_tree(args.root, args.backup)
    if args.repack:
        if not args.backup:
            parser.error("--backup is required with --repack")
        repack_tree(args.root, args.backup)
    if args.outline:
        if not args.backup:
            parser.error("--backup is required with --outline")
        outline_tree(args.root, args.backup, args.outline_radius)


if __name__ == "__main__":
    main()
