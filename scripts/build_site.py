#!/usr/bin/env python3
"""Builds the project website (GitHub Pages) into build/site.

site/index.html is the page; the screenshots come from docs/screenshots, and the "What's new" section is filled from
the newest version in CHANGELOG.md. The download buttons find the files of the latest GitHub release by themselves
(in the browser), so the site does not have to be rebuilt for a new release.
"""
import html
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "site"


def inline(text):
    """The Markdown used in the changelog: `code`, **bold**, *italic*, [link](url)."""
    parts = re.split(r"(`[^`]+`)", text)
    out = []
    for p in parts:
        if p.startswith("`") and p.endswith("`") and len(p) > 1:
            out.append("<code>" + html.escape(p[1:-1]) + "</code>")
            continue
        p = html.escape(p, quote=False)
        p = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", r'<a href="\2">\1</a>', p)
        p = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", p)
        p = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<em>\1</em>", p)
        out.append(p)
    return "".join(out)


def latest_changes():
    """(version, HTML) of the first section of CHANGELOG.md."""
    lines = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8").splitlines()
    version, body = None, []
    for line in lines:
        m = re.match(r"^##\s+(.+?)\s*$", line)
        if m:
            if version is not None:
                break
            version = m.group(1)
            continue
        if version is not None:
            body.append(line)

    # Items: "- text" with indented continuation lines and "  - " sub-items.
    items = []          # [text, [subitems]]
    for line in body:
        if not line.strip():
            continue
        if line.startswith("- "):
            items.append([line[2:].strip(), []])
        elif re.match(r"^\s+[-*] ", line) and items:
            items[-1][1].append(re.sub(r"^\s+[-*] ", "", line).strip())
        elif items:
            if items[-1][1]:
                items[-1][1][-1] += " " + line.strip()
            else:
                items[-1][0] += " " + line.strip()
    out = [f"      <h3>Éter {html.escape(version or '')}</h3>", "      <ul>"]
    for text, subs in items:
        li = "        <li>" + inline(text)
        if subs:
            li += "<ul>" + "".join("<li>" + inline(s) + "</li>" for s in subs) + "</ul>"
        out.append(li + "</li>")
    out.append("      </ul>")
    return version, "\n".join(out)


def main():
    if OUT.exists():
        shutil.rmtree(OUT)
    shutil.copytree(ROOT / "site", OUT)
    shutil.copytree(ROOT / "docs" / "screenshots", OUT / "screenshots")
    version, news = latest_changes()
    page = (OUT / "index.html").read_text(encoding="utf-8").replace("<!--WHATS_NEW-->", news)
    (OUT / "index.html").write_text(page, encoding="utf-8")
    (OUT / ".nojekyll").write_text("", encoding="utf-8")
    print(f"Website for Éter {version}: {OUT}")


if __name__ == "__main__":
    main()
