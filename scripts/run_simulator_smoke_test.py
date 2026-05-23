#!/usr/bin/env python3
"""Build and run the simulator smoke test against an isolated fs_ directory."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEVICE_ENVS = {"x4": "simulator", "x3": "simulator_x3"}
DEFAULT_BOOK = ROOT / "test" / "epubs" / "test_reader_rendering_matrix.epub"
JPEG_FIXTURE = ROOT / "test" / "epubs" / "test_jpeg_images.epub"
# Distinct cover sources (extracted from the JPEG fixture) paired with book
# titles. Used by --shelf to synthesize a populated Favorites collection so the
# multi-cover shelf grid can be eyeballed (X4 vs X3).
SHELF_COVERS = [
    ("Aurora Drift", "OEBPS/images/gradient_test.jpg"),
    ("Bramble Hollow", "OEBPS/images/centering_test.jpg"),
    ("Cobalt Reverie", "OEBPS/images/grayscale_test.jpg"),
    ("Dune Cipher", "OEBPS/images/cache_test_1.jpg"),
    ("Ember Atlas", "OEBPS/images/cache_test_2.jpg"),
    ("Frost Lantern", "OEBPS/images/jpeg_format.jpg"),
]


def program_path(pio_env: str) -> Path:
    return ROOT / ".pio" / "build" / pio_env / "program"
CRASH_PATTERNS = (
    "std::bad_alloc",
    "terminating due to uncaught exception",
    "Assertion failed",
    "Segmentation fault",
    "AddressSanitizer",
    "UndefinedBehaviorSanitizer",
)
THEMES = {
    "classic": 0,
    "lyra": 1,
    "lyra-extended": 2,
    "lyra_extended": 2,
    "lyra3": 2,
    "lyra-3-covers": 2,
    "roundedraff": 3,
    "rounded-raff": 3,
    "lyra-carousel": 4,
    "lyra_carousel": 4,
    "carousel": 4,
}


def build_simulator(pio_env: str) -> None:
    print(f"Building simulator ({pio_env})...", flush=True)
    proc = subprocess.run(["pio", "run", "-e", pio_env], cwd=ROOT)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)


def _slug(title: str) -> str:
    return "".join(ch.lower() if ch.isalnum() else "_" for ch in title).strip("_")


def make_cover_epub(dest_path: Path, title: str, cover_jpg: bytes) -> None:
    """Write a minimal EPUB that declares `cover.jpg` as its cover (both the
    EPUB2 <meta name="cover"> and EPUB3 properties="cover-image" forms, so the
    ContentOpfParser finds it either way)."""
    import zipfile

    slug = _slug(title)
    container = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n'
        '  <rootfiles><rootfile full-path="OEBPS/content.opf" '
        'media-type="application/oebps-package+xml"/></rootfiles>\n'
        '</container>\n'
    )
    opf = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid">\n'
        '  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">\n'
        f'    <dc:identifier id="uid">cover-fixture-{slug}</dc:identifier>\n'
        f'    <dc:title>{title}</dc:title>\n'
        '    <dc:language>en</dc:language>\n'
        '    <meta name="cover" content="cover-img"/>\n'
        '  </metadata>\n'
        '  <manifest>\n'
        '    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>\n'
        '    <item id="cover-img" href="cover.jpg" media-type="image/jpeg" properties="cover-image"/>\n'
        '    <item id="chap1" href="chap1.xhtml" media-type="application/xhtml+xml"/>\n'
        '  </manifest>\n'
        '  <spine><itemref idref="chap1"/></spine>\n'
        '</package>\n'
    )
    nav = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">\n'
        '<head><title>nav</title></head><body>\n'
        '<nav epub:type="toc"><ol><li><a href="chap1.xhtml">Chapter 1</a></li></ol></nav>\n'
        '</body></html>\n'
    )
    chap = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<html xmlns="http://www.w3.org/1999/xhtml"><head><title>{title}</title></head><body>\n'
        f'<h1>{title}</h1>\n<p>Synthetic shelf fixture for the cover grid layout check.</p>\n'
        '</body></html>\n'
    )
    with zipfile.ZipFile(dest_path, "w", zipfile.ZIP_DEFLATED) as z:
        # mimetype must be the first entry and stored uncompressed.
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", container)
        z.writestr("OEBPS/content.opf", opf)
        z.writestr("OEBPS/nav.xhtml", nav)
        z.writestr("OEBPS/chap1.xhtml", chap)
        z.writestr("OEBPS/cover.jpg", cover_jpg)


def build_cover_fixtures(books_dir: Path) -> list[str]:
    import zipfile

    sim_paths: list[str] = []
    with zipfile.ZipFile(JPEG_FIXTURE) as src:
        for title, member in SHELF_COVERS:
            cover = src.read(member)
            name = f"{_slug(title)}.epub"
            make_cover_epub(books_dir / name, title, cover)
            sim_paths.append(f"/books/{name}")
    return sim_paths


def write_collections(crosspoint_dir: Path, book_paths: list[str]) -> None:
    import json

    crosspoint_dir.mkdir(parents=True, exist_ok=True)
    doc = {
        "version": 1,
        "active": "favorites",
        "collections": [
            {
                "id": "favorites",
                "name": "Favorites",
                "sort": 0,
                "collapseSeries": True,
                "books": book_paths,
            }
        ],
    }
    (crosspoint_dir / "collections.json").write_text(json.dumps(doc))


def prepare_fs(temp_root: Path, book: Path, shelf: bool) -> str:
    fs_root = temp_root / "fs_"
    books_dir = fs_root / "books"
    books_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(book, books_dir / book.name)
    opened = f"/books/{book.name}"
    if shelf:
        cover_paths = build_cover_fixtures(books_dir)
        # Lead the shelf with real-cover fixtures, then the opened reader book
        # (a title-fallback card) so the grid shows both kinds of cell.
        write_collections(fs_root / ".crosspoint", [*cover_paths, opened])
    return opened


def run_smoke(args: argparse.Namespace) -> int:
    pio_env = DEVICE_ENVS[args.device]
    program = program_path(pio_env)

    book = Path(args.book).resolve()
    if not book.exists():
        print(f"Smoke test book not found: {book}", file=sys.stderr)
        return 2

    if args.build:
        build_simulator(pio_env)

    if not program.exists():
        print(f"Simulator binary not found: {program}", file=sys.stderr)
        print(f"Run: pio run -e {pio_env}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="crossink-sim-smoke-") as temp_dir_name:
        temp_root = Path(temp_dir_name)
        simulator_book_path = prepare_fs(temp_root, book, args.shelf)

        env = os.environ.copy()
        env["CROSSINK_SIMULATOR_SMOKE_TEST"] = "1"
        env["CROSSINK_SIMULATOR_SMOKE_BOOK"] = simulator_book_path
        env["CROSSINK_SIMULATOR_SMOKE_PAGE_TURNS"] = str(args.page_turns)
        if args.theme:
            env["CROSSINK_SIMULATOR_SMOKE_THEME"] = str(THEMES[args.theme])
        if args.headless:
            env.setdefault("SDL_VIDEODRIVER", "dummy")

        print(
            f"Running simulator smoke test ({args.device}/{pio_env}) "
            f"with isolated fs_: {temp_root / 'fs_'}",
            flush=True,
        )
        proc = subprocess.run(
            [str(program)],
            cwd=temp_root,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=args.timeout,
        )

    print(proc.stdout, end="")

    if proc.returncode != 0:
        print(f"Simulator smoke test failed with exit code {proc.returncode}", file=sys.stderr)
        return proc.returncode

    for pattern in CRASH_PATTERNS:
        if pattern in proc.stdout:
            print(f"Simulator smoke test output contained crash pattern: {pattern}", file=sys.stderr)
            return 2

    if "Simulator smoke test passed" not in proc.stdout:
        print("Simulator smoke test did not print its success marker", file=sys.stderr)
        return 2

    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", choices=sorted(DEVICE_ENVS), default="x4", help="Panel to simulate: x4 (800x480) or x3 (792x528)")
    parser.add_argument("--shelf", action="store_true", help="Seed a Favorites collection of cover-bearing fixtures to exercise the multi-cover shelf grid")
    parser.add_argument("--book", default=str(DEFAULT_BOOK), help="EPUB fixture to copy into the isolated simulator fs_")
    parser.add_argument("--timeout", type=int, default=45, help="Seconds before the simulator run is treated as hung")
    parser.add_argument("--page-turns", type=int, default=2, help="Number of EPUB page-forward taps to run")
    parser.add_argument("--theme", choices=sorted(THEMES), help="UI theme to use during the smoke test")
    parser.add_argument("--no-build", dest="build", action="store_false", help="Run the existing simulator binary")
    parser.add_argument("--window", dest="headless", action="store_false", help="Show the SDL window instead of using dummy video")
    parser.set_defaults(build=True, headless=True)
    return parser.parse_args()


def main() -> int:
    return run_smoke(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
