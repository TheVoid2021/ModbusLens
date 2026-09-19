#!/usr/bin/env python
"""M9-E E2 icon generation helper (maintainer tool, NOT part of the build).

Renders the canonical SVG master (assets/brand/icon.svg) at every required
size with the Qt SVG renderer (PyQt5) and assembles the committed
multi-resolution Windows asset (assets/brand/windows/ModbusLens.ico).

Fail-fast: any missing tool, failed render or failed write exits non-zero.
The normal CMake configure/build NEVER calls this script - the derived ICO
is committed, so end-user builds have no icon tool dependency.

Usage:  python scripts/make_icon.py
"""
import struct
import sys

SIZES = [16, 24, 32, 48, 64, 256]
SVG_PATH = "assets/brand/icon.svg"
ICO_PATH = "assets/brand/windows/ModbusLens.ico"


def fail(message):
    print("make_icon FAIL: " + message)
    sys.exit(1)


def check_tools():
    try:
        from PyQt5.QtSvg import QSvgRenderer  # noqa: F401
        from PyQt5.QtGui import QImage, QPainter  # noqa: F401
    except ImportError as exc:
        fail("PyQt5 (QtSvg/QtGui) unavailable: %s" % exc)
    try:
        import PIL  # noqa: F401
        import PIL.Image  # noqa: F401
    except ImportError as exc:
        fail("Pillow unavailable: %s" % exc)


def render_sizes():
    """Rasterize the SVG master at every required size (PyQt5 offscreen)."""
    from PyQt5.QtCore import QBuffer, QByteArray, Qt
    from PyQt5.QtGui import QImage, QPainter
    from PyQt5.QtSvg import QSvgRenderer
    from PyQt5.QtWidgets import QApplication

    app = QApplication.instance() or QApplication(sys.argv[:1])
    svg = open(SVG_PATH, "rb").read()
    renderer = QSvgRenderer(QByteArray(svg))
    if not renderer.isValid():
        fail("SVG master is not a valid renderable document")
    frames = {}
    for size in SIZES:
        image = QImage(size, size, QImage.Format_ARGB32)
        image.fill(0)
        painter = QPainter(image)
        renderer.render(painter)
        painter.end()
        frames[size] = image
    del app
    return frames


def qimage_to_pil(image):
    """Convert a QImage frame to a PIL RGBA image (via in-memory PNG)."""
    from PyQt5.QtCore import QBuffer
    buffer = QBuffer()
    buffer.open(QBuffer.ReadWrite)
    image.save(buffer, "PNG")
    import io as _io
    from PIL import Image
    return Image.open(_io.BytesIO(bytes(buffer.data()))).convert("RGBA")


def assemble_ico(frames):
    """Write the multi-frame ICO.

    Pillow >= 9.3 accepts append_images: the per-size SVG renders are used
    as the exact frames (best quality). The ICO is verified independently
    afterwards (verify_ico) - the plugin output is never trusted blindly.
    """
    from PIL import Image

    pil_frames = {size: qimage_to_pil(frames[size]) for size in SIZES}
    master = pil_frames[256]
    appended = [pil_frames[size] for size in sorted(SIZES, reverse=True)
                if size != 256]
    master.save(
        ICO_PATH, format="ICO",
        sizes=[(size, size) for size in sorted(SIZES, reverse=True)],
        append_images=appended)


def verify_ico():
    """Independent ICO directory audit (struct parse, not tool output)."""
    with open(ICO_PATH, "rb") as handle:
        blob = handle.read()
    if len(blob) < 6 or blob[:4] != b"\x00\x00\x01\x00":
        fail("ICO header invalid (reserved/type)")
    count = struct.unpack_from("<H", blob, 4)[0]
    if count != len(SIZES):
        fail("ICO frame count %d != required %d" % (count, len(SIZES)))
    seen = []
    offset = 6
    for _ in range(count):
        width, height = struct.unpack_from("<BB", blob, offset)
        width = 256 if width == 0 else width
        height = 256 if height == 0 else height
        seen.append((width, height))
        offset += 16
    missing = [s for s in SIZES if (s, s) not in seen]
    if missing:
        fail("ICO missing required frames: %s" % missing)
    print("make_icon OK: %s frames=%s" % (ICO_PATH, sorted(seen)))


def main():
    check_tools()
    frames = render_sizes()
    assemble_ico(frames)
    verify_ico()


if __name__ == "__main__":
    main()
