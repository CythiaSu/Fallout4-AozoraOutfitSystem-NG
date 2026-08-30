from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image

from defringe_mascots import add_white_outline, shift


DECORATION_SEEDS = {
    "phone.png": ((930, 590),),
    "watch.png": ((135, 545),),
    "picnic.png": ((170, 850),),
}

CLEAR_RECTANGLES = {
    # The holographic screen contains several disconnected antialias layers,
    # so clear its isolated source area instead of removing one component.
    "watch.png": ((730, 380, 1086, 735),),
}


def clear_rectangles(
    image: Image.Image,
    rectangles: tuple[tuple[int, int, int, int], ...],
) -> Image.Image:
    rgba = np.asarray(image.convert("RGBA")).copy()
    for x0, y0, x1, y1 in rectangles:
        rgba[y0:y1, x0:x1, 3] = 0
    return Image.fromarray(rgba, "RGBA")


def remove_alpha_component(image: Image.Image, seed: tuple[int, int]) -> Image.Image:
    rgba = np.asarray(image.convert("RGBA")).copy()
    foreground = rgba[..., 3] > 8
    width, height = image.size
    sx, sy = seed

    if not (0 <= sx < width and 0 <= sy < height):
        raise ValueError(f"Decoration seed is outside the image: {seed}")

    if not foreground[sy, sx]:
        nearest: tuple[int, int] | None = None
        for radius in range(1, 81):
            y0, y1 = max(0, sy - radius), min(height, sy + radius + 1)
            x0, x1 = max(0, sx - radius), min(width, sx + radius + 1)
            points = np.argwhere(foreground[y0:y1, x0:x1])
            if len(points):
                py, px = points[0]
                nearest = (x0 + int(px), y0 + int(py))
                break
        if nearest is None:
            raise ValueError(f"No decoration pixels found near seed: {seed}")
        sx, sy = nearest

    visited = np.zeros_like(foreground)
    queue: deque[tuple[int, int]] = deque(((sx, sy),))
    visited[sy, sx] = True
    while queue:
        x, y = queue.popleft()
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if (
                0 <= nx < width
                and 0 <= ny < height
                and foreground[ny, nx]
                and not visited[ny, nx]
            ):
                visited[ny, nx] = True
                queue.append((nx, ny))

    rgba[..., 3][visited] = 0
    return Image.fromarray(rgba, "RGBA")


def strip_existing_white_edge(image: Image.Image, reach: int = 10) -> Image.Image:
    rgba = np.asarray(image.convert("RGBA")).copy()
    rgb = rgba[..., :3].astype(np.int16)
    alpha = rgba[..., 3]

    transparent_reach = alpha == 0
    edge_ring = np.zeros_like(transparent_reach)
    for _ in range(reach):
        expanded = transparent_reach.copy()
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            expanded |= shift(transparent_reach, dy, dx)
        edge_ring |= expanded & (alpha > 0)
        transparent_reach = expanded

    spread = rgb.max(axis=2) - rgb.min(axis=2)
    near_white = (rgb.min(axis=2) >= 232) & (spread <= 28)
    rgba[..., 3][near_white & edge_ring] = 0
    return Image.fromarray(rgba, "RGBA")


def compose(source: Path, output: Path, outline_radius: int) -> None:
    names = ("phone.png", "watch.png", "picnic.png")
    images = {}
    for name in names:
        image = Image.open(source / name).convert("RGBA")
        image = clear_rectangles(image, CLEAR_RECTANGLES.get(name, ()))
        for seed in DECORATION_SEEDS.get(name, ()):
            image = remove_alpha_component(image, seed)
        images[name] = strip_existing_white_edge(image)

    width, height = images["phone.png"].size
    if any(image.size != (width, height) for image in images.values()):
        raise ValueError("Target mascot source images must share one canvas size")

    # Match the UI ratio: 184px image width with a 110px horizontal step.
    step = round(width * 110 / 184)
    canvas = Image.new("RGBA", (width + step * 2, height), (0, 0, 0, 0))

    # phone and picnic use the normal layer; watch is the centered top layer.
    canvas.alpha_composite(images["phone.png"], (0, 0))
    canvas.alpha_composite(images["picnic.png"], (step * 2, 0))
    canvas.alpha_composite(images["watch.png"], (step, 0))

    output.parent.mkdir(parents=True, exist_ok=True)
    add_white_outline(canvas, outline_radius).save(output, optimize=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--outline-radius", type=int, default=10)
    args = parser.parse_args()
    compose(args.source, args.output, args.outline_radius)


if __name__ == "__main__":
    main()
