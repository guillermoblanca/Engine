#!/usr/bin/env python3
"""Builds an HTML page from the artifacts a failed golden test leaves in qa_artifacts/.

  python3 tools/qa_report.py build/tests/qa_artifacts [out.html]

Each *.compare.png is expected | actual | difference (x8). Look at them, and if the change is intended run
  GB_UPDATE_GOLDEN=1 ctest -R "golden" --test-dir build
and commit the updated files under tests/golden.
"""
import html
import pathlib
import sys


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "qa_artifacts")
    out = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else root / "report.html")
    rows = []
    for compare in sorted(root.glob("*.compare.png")):
        name = compare.name[:-len(".compare.png")]
        rows.append(f"<section><h2>{html.escape(name)}</h2><p>expected | actual | difference (x8)</p>"
                    f"<img src='{html.escape(compare.name)}' style='width:100%;image-rendering:pixelated'></section>")
    for new in sorted(root.glob("*.actual.png")):
        name = new.name[:-len(".actual.png")]
        if not (root / f"{name}.compare.png").exists():
            rows.append(f"<section><h2>{html.escape(name)} (no reference yet)</h2><img src='{html.escape(new.name)}'></section>")
    for data in sorted(root.glob("*.actual.txt")):
        rows.append(f"<section><h2>{html.escape(data.name)}</h2><pre>{html.escape(data.read_text())}</pre></section>")
    body = "\n".join(rows) or "<p>No failures.</p>"
    out.write_text("<!doctype html><meta charset='utf-8'><title>GB QA report</title>"
                   "<body style='font-family:sans-serif;background:#111;color:#ddd;max-width:1200px;margin:auto'>"
                   f"<h1>GB QA report</h1>{body}</body>")
    print(f"wrote {out} ({len(rows)} entries)")


if __name__ == "__main__":
    main()
