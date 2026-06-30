#!/usr/bin/env python3
"""Prepare manga archives for PaperBadge firmware.

Outputs non-ZIP64 CBZ chunks plus an extracted optimized folder with manifest
and page-index sidecars. OCR is optional and disabled by default.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path


IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".webp"}


def natural_key(path: Path) -> list[object]:
    out: list[object] = []
    buf = ""
    for ch in path.as_posix().lower():
        if ch.isdigit():
            buf += ch
        else:
            if buf:
                out.append(int(buf))
                buf = ""
            out.append(ch)
    if buf:
        out.append(int(buf))
    return out


@dataclass
class PageRecord:
    index: int
    source_name: str
    optimized_name: str
    original_size: tuple[int, int] | None = None
    optimized_size: tuple[int, int] | None = None
    slice: dict[str, int] | None = None


def require_pillow():
    try:
        from PIL import Image

        return Image
    except ImportError as exc:
        raise SystemExit(
            "Pillow is required for image preprocessing. Install with: python3 -m pip install pillow"
        ) from exc


def extract_archive(source: Path, work: Path) -> list[Path]:
    suffix = source.suffix.lower()
    if suffix in {".cbz", ".zip"}:
        with zipfile.ZipFile(source) as zf:
            zf.extractall(work)
    elif suffix in {".cbr", ".rar"}:
        tool = shutil.which("unar") or shutil.which("bsdtar")
        if not tool:
            raise SystemExit("CBR/RAR input requires `unar` or `bsdtar` installed on the Mac.")
        if Path(tool).name == "unar":
            subprocess.run([tool, "-quiet", "-o", str(work), str(source)], check=True)
        else:
            subprocess.run([tool, "-xf", str(source), "-C", str(work)], check=True)
    else:
        raise SystemExit(f"Unsupported input format: {source.suffix}")
    return sorted(
        [p for p in work.rglob("*") if p.is_file() and p.suffix.lower() in IMAGE_EXTS],
        key=natural_key,
    )


def quantize_gray16(image):
    image = image.convert("L")
    return image.point(lambda v: int(round(v / 17)) * 17).convert("RGB")


def process_page(src: Path, dest_dir: Path, args, page_index: int) -> list[PageRecord]:
    records: list[PageRecord] = []
    if args.downscale == "preserve" and not args.grayscale16:
        dest = dest_dir / f"{page_index:05d}{src.suffix.lower()}"
        shutil.copy2(src, dest)
        records.append(PageRecord(page_index, src.as_posix(), dest.name))
        return records

    Image = require_pillow()
    with Image.open(src) as image:
        original_size = image.size
        image = image.convert("RGB")
        if args.grayscale16:
            image = quantize_gray16(image)
        if args.downscale == "portrait":
            image.thumbnail((args.portrait_width, args.portrait_height), Image.Resampling.LANCZOS)
            dest = dest_dir / f"{page_index:05d}.jpg"
            image.save(dest, "JPEG", quality=88, optimize=True)
            records.append(PageRecord(page_index, src.as_posix(), dest.name, original_size, image.size))
        elif args.downscale == "landscape-slices":
            width = args.landscape_width
            height = max(1, int(image.height * (width / image.width)))
            resized = image.resize((width, height), Image.Resampling.LANCZOS)
            slice_h = max(1, min(args.landscape_height, (height + args.slices - 1) // args.slices))
            for slice_idx, y0 in enumerate(range(0, height, slice_h)):
                crop = resized.crop((0, y0, width, min(height, y0 + slice_h)))
                dest = dest_dir / f"{page_index:05d}_s{slice_idx:02d}.jpg"
                crop.save(dest, "JPEG", quality=88, optimize=True)
                records.append(
                    PageRecord(
                        page_index,
                        src.as_posix(),
                        dest.name,
                        original_size,
                        crop.size,
                        {"x": 0, "y": y0, "w": width, "h": crop.height},
                    )
                )
        else:
            dest = dest_dir / f"{page_index:05d}.jpg"
            image.save(dest, "JPEG", quality=90, optimize=True)
            records.append(PageRecord(page_index, src.as_posix(), dest.name, original_size, image.size))
    return records


def write_cbz_chunks(records: list[PageRecord], image_dir: Path, out_dir: Path, stem: str, max_bytes: int, zip64: str):
    chunks: list[dict[str, object]] = []
    current: list[PageRecord] = []
    current_size = 0
    chunk_id = 1

    def flush() -> None:
        nonlocal current, current_size, chunk_id
        if not current:
            return
        cbz = out_dir / f"{stem}_part{chunk_id:03d}.cbz"
        allow_zip64 = zip64 == "always" or zip64 == "auto"
        with zipfile.ZipFile(cbz, "w", compression=zipfile.ZIP_DEFLATED, allowZip64=allow_zip64) as zf:
            for rec in current:
                zf.write(image_dir / rec.optimized_name, rec.optimized_name)
        chunks.append({"file": cbz.name, "pages": len(current), "bytes": cbz.stat().st_size})
        current = []
        current_size = 0
        chunk_id += 1

    for rec in records:
        size = (image_dir / rec.optimized_name).stat().st_size
        if current and current_size + size > max_bytes:
            flush()
        if size > max_bytes and zip64 == "never":
            raise SystemExit(f"Single optimized page exceeds --max-mb limit: {rec.optimized_name}")
        current.append(rec)
        current_size += size
    flush()
    return chunks


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="input .cbz, .zip, optionally .cbr/.rar")
    parser.add_argument("--out", type=Path, default=Path("paperbadge_manga_out"))
    parser.add_argument("--max-mb", type=int, default=150)
    parser.add_argument("--zip64", choices=["never", "auto", "always"], default="never")
    parser.add_argument("--downscale", choices=["preserve", "portrait", "landscape-slices"], default="preserve")
    parser.add_argument("--grayscale16", action="store_true")
    parser.add_argument("--keep-originals", action="store_true")
    parser.add_argument("--slices", type=int, default=4)
    parser.add_argument("--portrait-width", type=int, default=540)
    parser.add_argument("--portrait-height", type=int, default=960)
    parser.add_argument("--landscape-width", type=int, default=960)
    parser.add_argument("--landscape-height", type=int, default=540)
    parser.add_argument("--ocr", choices=["none", "gemini"], default="none")
    parser.add_argument("--ocr-model", default="gemini-1.5-flash")
    parser.add_argument("--ocr-translate", action="store_true", help="ask OCR provider for English translation and study hints")
    args = parser.parse_args()

    source = args.input.expanduser().resolve()
    if not source.exists():
        raise SystemExit(f"Input not found: {source}")
    args.out.mkdir(parents=True, exist_ok=True)
    image_dir = args.out / "optimized_pages"
    image_dir.mkdir(exist_ok=True)
    originals_dir = args.out / "originals"

    with tempfile.TemporaryDirectory() as tmp:
        pages = extract_archive(source, Path(tmp))
        if not pages:
            raise SystemExit("No image pages found in archive.")
        if args.keep_originals:
            originals_dir.mkdir(exist_ok=True)
            for p in pages:
                shutil.copy2(p, originals_dir / p.name)
        records: list[PageRecord] = []
        for idx, page in enumerate(pages):
            records.extend(process_page(page, image_dir, args, idx))

    max_bytes = args.max_mb * 1024 * 1024
    chunks = write_cbz_chunks(records, image_dir, args.out, source.stem, max_bytes, args.zip64)
    if args.ocr != "none":
        run_ocr(args, image_dir, records, args.out)

    page_index = [
        {
            "index": rec.index,
            "source_name": rec.source_name,
            "optimized_name": rec.optimized_name,
            "original_size": rec.original_size,
            "optimized_size": rec.optimized_size,
            "slice": rec.slice,
        }
        for rec in records
    ]
    manifest = {
        "manga_id": source.stem,
        "source_file": str(source),
        "output_files": chunks,
        "page_count": len({r.index for r in records}),
        "asset_count": len(records),
        "page_order": [r.optimized_name for r in records],
        "firmware_compatibility": {
            "target_max_mb": args.max_mb,
            "zip64": args.zip64,
            "recommended": "CBZ chunks under 150 MB, JPEG or PNG pages, no ZIP64",
            "landscape_viewport": {"width": args.landscape_width, "height": args.landscape_height, "slices": args.slices},
            "portrait_viewport": {"width": args.portrait_width, "height": args.portrait_height},
        },
    }
    (args.out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    (args.out / "page_index.json").write_text(json.dumps(page_index, ensure_ascii=False, indent=2) + "\n")
    print(args.out / "manifest.json")
    return 0


def run_ocr(args, image_dir: Path, records: list[PageRecord], out_dir: Path) -> None:
    if args.ocr == "gemini":
        api_key = os.environ.get("GEMINI_API_KEY") or os.environ.get("GOOGLE_API_KEY")
        if not api_key:
            raise SystemExit("--ocr gemini requires GEMINI_API_KEY or GOOGLE_API_KEY in the environment.")
        try:
            import google.generativeai as genai  # type: ignore
        except ImportError as exc:
            raise SystemExit(
                "Gemini OCR requires the optional SDK: python3 -m pip install google-generativeai"
            ) from exc
        genai.configure(api_key=api_key)
        model = genai.GenerativeModel(args.ocr_model)
        ocr_dir = out_dir / "ocr"
        ocr_dir.mkdir(exist_ok=True)
        regions = []
        for rec in records:
            image_path = image_dir / rec.optimized_name
            mime = {
                ".jpg": "image/jpeg",
                ".jpeg": "image/jpeg",
                ".png": "image/png",
                ".webp": "image/webp",
            }.get(image_path.suffix.lower(), "image/jpeg")
            prompt = (
                "Extract Japanese text from this manga page or slice. Return compact JSON with "
                "raw_japanese_text, english_translation, page_number, slice_or_region, "
                "vocabulary_candidates, grammar_candidates, kanji_candidates, confidence, and notes. "
                "Use page-level text if region structure is uncertain."
            )
            if not args.ocr_translate:
                prompt += " Keep english_translation brief or empty if translation is uncertain."
            response = model.generate_content([prompt, {"mime_type": mime, "data": image_path.read_bytes()}])
            text = getattr(response, "text", "") or ""
            regions.append(
                {
                    "@type": "OCRRegion",
                    "page_index": rec.index,
                    "source_name": rec.source_name,
                    "asset": rec.optimized_name,
                    "slice": rec.slice,
                    "provider": "gemini",
                    "model": args.ocr_model,
                    "raw": text,
                    "metadata": {
                        "book": args.input.name,
                        "ocr_translate": bool(args.ocr_translate),
                    },
                }
            )
        payload = {"@context": "https://paperbadge.local/schema/manga_ocr.jsonld", "regions": regions}
        (ocr_dir / "text.json").write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n")
        for name in ["vocab.json", "concepts.json", "page_map.json"]:
            (ocr_dir / name).write_text(json.dumps({"items": []}, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
