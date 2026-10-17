#!/usr/bin/env python3
"""Turns the Creator Kit guide (Markdown subset + SVG pictures) into ONE self-contained HTML file.

  python tools/creator_kit_guide.py build [--md docs/CREATOR_KIT.md] [--out docs/CREATOR_KIT.html]
  python tools/creator_kit_guide.py check [--md ...] [--out ...]
  python tools/creator_kit_guide.py selftest

build     writes the HTML (UTF-8, LF, final newline, no timestamps, no absolute paths).
check     exit 0 if the file on disk equals what build would write, 1 if not (first differing
          line is printed), 2 on errors.
selftest   in-memory cases, exit 0 only if all pass.
Default paths are relative to the repo root (the folder above tools/). Paths given on the
command line are relative to the current folder.

Markdown subset (anything else is an error with the line number, exit 2):
  headings # .. ####   (id = slug of the text, made unique with -2, -3, ...)
  paragraphs           (lines joined with a space)
  lists                "- " and "1. ", one nesting level (indent 2 or 3 spaces)
  fenced code          ``` ... ```
  inline               `code`, **bold**, *italic*, [text](https://...), [text](other.md)
                       (relative .md links stay as text, target shown in parentheses)
  images               ![alt](assets/kit-guide-<name>.svg) alone on a line; the SVG is inlined
                       (ids prefixed per image, so they never clash)
  tables               pipe tables with a header separator line
  notes                "> " block
The HTML has no scripts and no outside files. Standard library only.
"""
import argparse
import html
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_MD = os.path.join(ROOT, "docs", "CREATOR_KIT.md")
DEFAULT_OUT = os.path.join(ROOT, "docs", "CREATOR_KIT.html")
IMG_RE = re.compile(r"^!\[([^\]]*)\]\((assets/kit-guide-[A-Za-z0-9_.-]+\.svg)\)$")
HEAD_RE = re.compile(r"^(#{1,4}) +(\S.*?)(?: +#+)?$")
ITEM_RE = re.compile(r"^( {0,3})(?:(-)|(\d+)\.) +(\S.*)$")
FENCE_RE = re.compile(r"^```[A-Za-z0-9_+-]*\s*$")
SEP_CELL_RE = re.compile(r"^:?-{1,}:?$")

CSS = """\
:root { --bg: #ffffff; --fg: #1f2328; --muted: #59636e; --line: #d1d9e0; --code-bg: #f3f4f6;
  --note-bg: #eef4ff; --note-line: #3b6fd4; --link: #0b5cd5; }
@media (prefers-color-scheme: dark) {
  :root { --bg: #14171a; --fg: #e6e8ea; --muted: #9aa4ae; --line: #3a4148; --code-bg: #232a30;
    --note-bg: #1b2636; --note-line: #6c9bf0; --link: #7fb0ff; }
}
html { background: var(--bg); }
body { background: var(--bg); color: var(--fg); margin: 0; padding: 24px 16px 64px;
  font: 16px/1.6 system-ui, -apple-system, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
main { max-width: 46em; margin: 0 auto; }
h1, h2, h3, h4 { line-height: 1.25; margin: 1.8em 0 0.6em; }
h1 { font-size: 2em; margin-top: 0; }
h2 { font-size: 1.5em; border-bottom: 1px solid var(--line); padding-bottom: 0.2em; }
h3 { font-size: 1.2em; }
h4 { font-size: 1.05em; }
a { color: var(--link); }
p, ul, ol, pre, table, blockquote, figure { margin: 0 0 1em; }
ul, ol { padding-left: 1.6em; }
li > ul, li > ol { margin: 0.25em 0 0; }
code { background: var(--code-bg); border-radius: 4px; padding: 0.1em 0.35em;
  font-family: ui-monospace, SFMono-Regular, Consolas, "Liberation Mono", monospace; font-size: 0.9em; }
pre { background: var(--code-bg); border-radius: 6px; padding: 12px 14px; overflow-x: auto; }
pre code { background: none; padding: 0; font-size: 0.88em; }
table { border-collapse: collapse; display: block; overflow-x: auto; max-width: 100%; }
th, td { border: 1px solid var(--line); padding: 6px 10px; text-align: left; vertical-align: top; }
th { background: var(--code-bg); }
blockquote { background: var(--note-bg); border-left: 4px solid var(--note-line);
  padding: 0.6em 1em; border-radius: 0 6px 6px 0; }
blockquote p:last-child { margin-bottom: 0; }
figure { text-align: center; }
figure svg { display: block; margin: 0 auto; height: auto; }
figcaption { color: var(--muted); font-size: 0.9em; margin-top: 0.4em; }
nav.toc { border: 1px solid var(--line); border-radius: 6px; padding: 0.6em 1em; margin: 0 0 1.5em; }
nav.toc strong { display: block; margin-bottom: 0.2em; }
nav.toc ol { margin: 0; }
"""


class GuideError(Exception):
    def __init__(self, line, msg):
        Exception.__init__(self, msg)
        self.line = line
        self.msg = msg


# ---------------------------------------------------------------- inline

def plain_text(s):
    s = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", s)
    return s.replace("`", "").replace("*", "")


def slugify(s):
    s = plain_text(s).lower()
    s = re.sub(r"[^a-z0-9 _-]", "", s)
    s = re.sub(r"[ _]+", "-", s.strip())
    s = re.sub(r"-{2,}", "-", s).strip("-")
    return s or "section"


def inline(s, lineno):
    if "![" in s:
        raise GuideError(lineno, "images must stand alone on their own line")
    codes = []

    def keep(m):
        codes.append("<code>" + html.escape(m.group(1), quote=False) + "</code>")
        return "\x00%d\x00" % (len(codes) - 1)

    s = re.sub(r"`([^`]+)`", keep, s)
    if "`" in s:
        raise GuideError(lineno, "unbalanced backtick")
    s = html.escape(s, quote=True)

    def link(m):
        text, url = m.group(1), html.unescape(m.group(2))
        if re.match(r"^https?://[^\s\"'<>]+$", url):
            return '<a href="%s" rel="noopener">%s</a>' % (html.escape(url, quote=True), text)
        if url.startswith("#") and re.match(r"^#[a-z0-9-]+$", url):
            return '<a href="%s">%s</a>' % (url, text)
        if re.match(r"^[^:\s]+\.md(#[A-Za-z0-9_-]+)?$", url):
            return "%s (<code>%s</code>)" % (text, html.escape(url, quote=False))
        raise GuideError(lineno, "unsupported link target: %s" % url)

    s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, s)
    s = re.sub(r"\*\*(?=\S)(.+?)(?<=\S)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"(?<![\w*])\*(?=\S)(.+?)(?<=\S)\*(?![\w*])", r"<em>\1</em>", s)
    return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], s)


# ---------------------------------------------------------------- svg

def process_svg(text, alt, n, name, lineno):
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    if text.startswith("\ufeff"):
        text = text[1:]
    head = text
    while True:
        h2 = head.lstrip()
        h2 = re.sub(r"^<\?xml.*?\?>", "", h2, flags=re.S)
        h2 = re.sub(r"^<!--.*?-->", "", h2, flags=re.S)
        h2 = re.sub(r"^<!DOCTYPE[^\[>]*>", "", h2, flags=re.S | re.I)
        if h2 == head:
            break
        head = h2
    text = head.strip()
    if not text.startswith("<svg"):
        raise GuideError(lineno, "%s: no <svg> root element" % name)
    if "[" in text[:text.find(">")] and "<!DOCTYPE" in text[:200]:
        raise GuideError(lineno, "%s: DOCTYPE with internal subset" % name)
    try:
        ET.fromstring(text)
    except ET.ParseError as e:
        raise GuideError(lineno, "%s: not well-formed XML (%s)" % (name, e))
    low = text.lower()
    if "<script" in low or "<foreignobject" in low or re.search(r"\son[a-z]+\s*=", low):
        raise GuideError(lineno, "%s: scripts / foreignObject / event handlers are not allowed" % name)
    if re.search(r"""href\s*=\s*["'](?!#)""", text) or re.search(r"url\(\s*[\"']?(?!#)(?!\s)[a-z]+:", low):
        raise GuideError(lineno, "%s: outside references are not allowed" % name)
    m = re.match(r"<svg\b[^>]*>", text, flags=re.S)
    root = m.group(0)
    if not re.search(r"\sviewBox\s*=", root):
        raise GuideError(lineno, "%s: <svg> has no viewBox" % name)
    attrs = re.sub(r"\s(?:width|height|role|aria-label|style)\s*=\s*(?:\"[^\"]*\"|'[^']*')", "", root[4:])
    new_root = ('<svg role="img" aria-label="%s" width="100%%" style="max-width:720px;height:auto"%s'
                % (html.escape(alt, quote=True), attrs))
    body = new_root + text[len(root):]
    prefix = "img%d-" % n
    body = re.sub(r"""(?<![\w:-])(id\s*=\s*)(["'])([^"']*)\2""",
                  lambda k: "%s%s%s%s%s" % (k.group(1), k.group(2), prefix, k.group(3), k.group(2)), body)
    body = re.sub(r"""(href\s*=\s*)(["'])#([^"']*)\2""",
                  lambda k: "%s%s#%s%s%s" % (k.group(1), k.group(2), prefix, k.group(3), k.group(2)), body)
    body = re.sub(r"""url\(\s*(["']?)#([^)"'\s]+)\1\s*\)""",
                  lambda k: "url(%s#%s%s%s)" % (k.group(1), prefix, k.group(2), k.group(1)), body)
    return body


# ---------------------------------------------------------------- blocks

def split_cells(line, lineno):
    s = line.strip()
    if not (s.startswith("|") and s.endswith("|") and len(s) > 1):
        raise GuideError(lineno, "table rows must start and end with '|'")
    s = s[1:-1].replace("\\|", "\x01")
    return [c.strip().replace("\x01", "|") for c in s.split("|")]


def build_html(md_text, read_svg):
    """md_text -> HTML text. read_svg(relative_path) -> svg text or raises OSError."""
    if md_text.startswith("\ufeff"):
        md_text = md_text[1:]
    lines = md_text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    out = []
    ids = {}
    toc = []
    state = {"title": None, "toc_at": None, "img": 0}
    i = 0
    n = len(lines)

    def starts_block(ln):
        return bool(HEAD_RE.match(ln) or FENCE_RE.match(ln) or ITEM_RE.match(ln)
                    or ln.startswith("|") or ln.startswith(">") or IMG_RE.match(ln))

    while i < n:
        ln = lines[i]
        no = i + 1
        if ln.strip() == "":
            i += 1
            continue
        if "\t" in ln and not ln.startswith("```"):
            raise GuideError(no, "tab character (use spaces)")
        # fenced code
        if ln.startswith("```"):
            if not FENCE_RE.match(ln):
                raise GuideError(no, "unsupported code fence")
            j = i + 1
            code = []
            while j < n and not lines[j].startswith("```"):
                code.append(lines[j])
                j += 1
            if j >= n:
                raise GuideError(no, "code block is not closed")
            out.append("<pre><code>" + html.escape("\n".join(code), quote=False) + "</code></pre>")
            i = j + 1
            continue
        # headings
        m = HEAD_RE.match(ln)
        if m:
            level, text = len(m.group(1)), m.group(2)
            base = slugify(text)
            slug, k = base, 2
            while slug in ids:
                slug = "%s-%d" % (base, k)
                k += 1
            ids[slug] = no
            out.append('<h%d id="%s">%s</h%d>' % (level, slug, inline(text, no), level))
            if state["title"] is None:
                state["title"] = plain_text(text)
                state["toc_at"] = len(out)
            if level == 2:
                toc.append((slug, inline(text, no)))
            i += 1
            continue
        if re.match(r"^#+\S", ln) or re.match(r"^#{5,}", ln):
            raise GuideError(no, "unsupported heading (use '# ' to '#### ' followed by a space)")
        if re.match(r"^\s*([*+])\s+\S", ln):
            raise GuideError(no, "unsupported list marker (use '- ')")
        if re.match(r"^(-{3,}|={3,}|_{3,}|\*{3,})\s*$", ln):
            raise GuideError(no, "horizontal rules / setext headings are not supported")
        if re.match(r"^</?[A-Za-z!]", ln):
            raise GuideError(no, "raw HTML is not supported")
        if re.match(r"^\[[^\]]+\]:\s", ln):
            raise GuideError(no, "reference-style links are not supported")
        # image
        if ln.startswith("!["):
            m = IMG_RE.match(ln.strip())
            if not m:
                raise GuideError(no, "image must be ![alt](assets/kit-guide-<name>.svg) alone on its line")
            alt, rel = m.group(1), m.group(2)
            try:
                svg = read_svg(rel)
            except OSError:
                raise GuideError(no, "image file not found: %s" % rel)
            state["img"] += 1
            body = process_svg(svg, alt, state["img"], rel, no)
            cap = "<figcaption>%s</figcaption>" % html.escape(alt, quote=False) if alt else ""
            out.append("<figure>\n%s\n%s</figure>" % (body, cap + "\n" if cap else ""))
            i += 1
            continue
        # table
        if ln.startswith("|"):
            if i + 1 >= n or not lines[i + 1].startswith("|"):
                raise GuideError(no, "table needs a header separator line")
            head = split_cells(ln, no)
            sep = split_cells(lines[i + 1], no + 1)
            if len(sep) != len(head) or not all(SEP_CELL_RE.match(c) for c in sep):
                raise GuideError(no + 1, "bad table header separator")
            aligns = []
            for c in sep:
                aligns.append("center" if c.startswith(":") and c.endswith(":") else
                              "right" if c.endswith(":") else "")
            rows = []
            j = i + 2
            while j < n and lines[j].startswith("|"):
                cells = split_cells(lines[j], j + 1)
                if len(cells) != len(head):
                    raise GuideError(j + 1, "table row has %d cells, header has %d" % (len(cells), len(head)))
                rows.append((j + 1, cells))
                j += 1

            def td(tag, c, a, lno):
                st = ' style="text-align:%s"' % a if a else ""
                return "<%s%s>%s</%s>" % (tag, st, inline(c, lno), tag)

            t = ["<table>", "<thead>", "<tr>"]
            t += [td("th", c, aligns[k], no) for k, c in enumerate(head)]
            t += ["</tr>", "</thead>", "<tbody>"]
            for lno, cells in rows:
                t.append("<tr>" + "".join(td("td", c, aligns[k], lno) for k, c in enumerate(cells)) + "</tr>")
            t += ["</tbody>", "</table>"]
            out.append("\n".join(t))
            i = j
            continue
        # note block
        if ln.startswith(">"):
            paras, cur = [], []
            j = i
            while j < n and lines[j].startswith(">"):
                if not re.match(r"^>( .*)?$", lines[j]):
                    raise GuideError(j + 1, "note lines must start with '> '")
                txt = lines[j][1:].strip()
                if txt == "":
                    if cur:
                        paras.append(cur)
                    cur = []
                else:
                    if re.match(r"^(#|- |\d+\. |```|\|)", txt):
                        raise GuideError(j + 1, "only plain text is supported inside a note block")
                    cur.append((j + 1, txt))
                j += 1
            if cur:
                paras.append(cur)
            if not paras:
                raise GuideError(no, "empty note block")
            ps = ["<p>%s</p>" % inline(" ".join(t for _, t in p), p[0][0]) for p in paras]
            out.append("<blockquote>\n" + "\n".join(ps) + "\n</blockquote>")
            i = j
            continue
        # lists
        if ITEM_RE.match(ln):
            items = []  # (no, ordered, start, text, children[(no, ordered, start, text)])
            j = i
            while j < n:
                cur = lines[j]
                if cur.strip() == "":
                    break
                if "\t" in cur:
                    raise GuideError(j + 1, "tab character (use spaces)")
                m = ITEM_RE.match(cur)
                if m:
                    ind = len(m.group(1))
                    ordered = m.group(2) is None
                    entry = (j + 1, ordered, m.group(3), m.group(4).strip())
                    if ind == 0:
                        items.append([entry, []])
                    elif ind in (2, 3):
                        if not items:
                            raise GuideError(j + 1, "nested item without a parent item")
                        items[-1][1].append(entry)
                    else:
                        raise GuideError(j + 1, "list indent must be 0, 2 or 3 spaces (one nesting level)")
                elif cur.startswith("  ") and items:
                    if re.match(r"^\s*(-|\d+\.) +\S", cur):
                        raise GuideError(j + 1, "list indent must be 0, 2 or 3 spaces (one nesting level)")
                    t = cur.strip()
                    if items[-1][1]:
                        c = items[-1][1][-1]
                        items[-1][1][-1] = (c[0], c[1], c[2], c[3] + " " + t)
                    else:
                        e = items[-1][0]
                        items[-1][0] = (e[0], e[1], e[2], e[3] + " " + t)
                else:
                    break
                j += 1

            def render(entries):
                res, k = [], 0
                while k < len(entries):
                    ordered = entries[k][1]
                    grp = []
                    while k < len(entries) and entries[k][1] == ordered:
                        grp.append(entries[k])
                        k += 1
                    tag = "ol" if ordered else "ul"
                    start = ""
                    if ordered and grp[0][2] not in (None, "1"):
                        start = ' start="%d"' % int(grp[0][2])
                    res.append("<%s%s>" % (tag, start))
                    res += ["<li>%s</li>" % inline(e[3], e[0]) for e in grp]
                    res.append("</%s>" % tag)
                return res

            # group top-level items by type, children inside the last item of the group
            top = []
            k = 0
            while k < len(items):
                ordered = items[k][0][1]
                tag = "ol" if ordered else "ul"
                start = ""
                if ordered and items[k][0][2] not in (None, "1"):
                    start = ' start="%d"' % int(items[k][0][2])
                top.append("<%s%s>" % (tag, start))
                while k < len(items) and items[k][0][1] == ordered:
                    e, kids = items[k]
                    li = "<li>" + inline(e[3], e[0])
                    if kids:
                        li += "\n" + "\n".join(render(kids)) + "\n"
                    top.append(li + "</li>")
                    k += 1
                top.append("</%s>" % tag)
            out.append("\n".join(top))
            i = j
            continue
        # paragraph
        if ln.startswith(" "):
            raise GuideError(no, "unexpected indentation")
        parts = [(no, ln.strip())]
        j = i + 1
        while j < n and lines[j].strip() != "" and not starts_block(lines[j]):
            if lines[j].startswith(" "):
                raise GuideError(j + 1, "unexpected indentation")
            if lines[j].startswith("```"):
                break
            parts.append((j + 1, lines[j].strip()))
            j += 1
        out.append("<p>%s</p>" % inline(" ".join(t for _, t in parts), no))
        i = j

    if state["title"] is None:
        raise GuideError(1, "the guide needs a heading")
    if toc:
        nav = ['<nav class="toc" aria-label="Contents">', "<strong>Contents</strong>", "<ol>"]
        nav += ['<li><a href="#%s">%s</a></li>' % (s, t) for s, t in toc]
        nav += ["</ol>", "</nav>"]
        out.insert(state["toc_at"], "\n".join(nav))
    doc = ["<!doctype html>", '<html lang="en">', "<head>", '<meta charset="utf-8">',
           '<meta name="viewport" content="width=device-width, initial-scale=1">',
           "<title>%s</title>" % html.escape(state["title"], quote=False),
           "<style>", CSS.rstrip("\n"), "</style>", "</head>", "<body>", "<main>"]
    doc += out
    doc += ["</main>", "</body>", "</html>"]
    return "\n".join(doc) + "\n"


def build_file(md_path):
    try:
        with open(md_path, "rb") as f:
            raw = f.read()
    except OSError as e:
        raise GuideError(0, "cannot read %s (%s)" % (md_path, e.strerror))
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        raise GuideError(0, "%s is not UTF-8" % md_path)
    base = os.path.dirname(os.path.abspath(md_path))

    def read_svg(rel):
        with open(os.path.join(base, *rel.split("/")), "rb") as f:
            return f.read().decode("utf-8")
    try:
        return build_html(text, read_svg)
    except UnicodeDecodeError:
        raise GuideError(0, "an SVG file is not UTF-8")


# ---------------------------------------------------------------- cli

def first_diff(a, b):
    la, lb = a.split("\n"), b.split("\n")
    for k in range(max(len(la), len(lb))):
        x = la[k] if k < len(la) else "<end of file>"
        y = lb[k] if k < len(lb) else "<end of file>"
        if x != y:
            return k + 1, x, y
    return 0, "", ""


def cmd_build(md, out):
    text = build_file(md)
    with open(out, "wb") as f:
        f.write(text.encode("utf-8"))
    print("creator_kit_guide: wrote %s (%d bytes)" % (os.path.basename(out), len(text.encode("utf-8"))))
    return 0


def compare(disk, text, name="file"):
    """disk: text on disk or None. Returns (exit code, message lines)."""
    if disk is None:
        return 1, ["creator_kit_guide: check FAILED: %s does not exist" % name]
    if disk == text:
        return 0, ["creator_kit_guide: check ok (%s is up to date)" % name]
    ln, a, b = first_diff(disk, text)
    return 1, ["creator_kit_guide: check FAILED: %s differs from the build, first difference at line %d" % (name, ln),
               "  on disk: %s" % a[:200], "  build  : %s" % b[:200]]


def cmd_check(md, out):
    text = build_file(md)
    try:
        with open(out, "rb") as f:
            disk = f.read().decode("utf-8")
    except OSError:
        disk = None
    except UnicodeDecodeError:
        disk = "<not UTF-8>"
    rc, msg = compare(disk, text, os.path.basename(out))
    print(chr(10).join(msg))
    return rc


def selftest():
    svg_a = ('<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" '
             '"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">\n'
             '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10" width="400" height="300">'
             '<defs><linearGradient id="g"><stop offset="0" stop-color="#fff"/></linearGradient></defs>'
             '<rect id="r" width="10" height="10" fill="url(#g)"/><use href="#r"/></svg>')
    svg_b = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 4 4"><linearGradient id="g">'
             '<stop offset="1" stop-color="#000"/></linearGradient><circle r="2" fill="url(#g)"/></svg>')
    files = {"assets/kit-guide-a.svg": svg_a, "assets/kit-guide-b.svg": svg_b}

    def rd(rel):
        if rel in files:
            return files[rel]
        raise OSError("missing")

    def b(md):
        return build_html(md, rd)

    def raises(md, frag, line=None):
        try:
            b(md)
        except GuideError as e:
            return frag in e.msg and (line is None or e.line == line)
        return False

    results = []

    def case(name, fn):
        try:
            ok = bool(fn())
        except Exception as e:  # noqa
            ok = False
            name += " (%s: %s)" % (type(e).__name__, e)
        results.append(ok)
        print("%s %s" % ("ok  " if ok else "FAIL", name))

    case("document skeleton and title", lambda: (lambda h: h.startswith("<!doctype html>\n<html lang=\"en\">")
         and "<title>My Guide</title>" in h and h.endswith("</html>\n") and "<script" not in h
         and "\r" not in h)(b("# My Guide\n\nHello.\n")))
    case("heading ids unique and TOC lists ## only", lambda: (lambda h: 'id="intro"' in h and 'id="intro-2"' in h
         and h.count('<li><a href="#') == 2 and 'id="my-guide"' in h)(b("# My Guide\n\n## Intro\n\n### Sub\n\n## Intro\n")))
    case("TOC sits after the first heading", lambda: (lambda h: h.index("</h1>") < h.index('<nav class="toc"')
         < h.index("<h2"))(b("# T\n\nintro\n\n## A\n")))
    case("paragraph lines joined with a space", lambda: "<p>one two</p>" in b("# T\n\none\ntwo\n"))
    case("list nesting one level", lambda: "<li>a\n<ul>\n<li>a1</li>\n<li>a2</li>\n</ul>\n</li>" in
         b("# T\n\n- a\n  - a1\n   - a2\n- b\n") and "<ol>" in b("# T\n\n1. x\n2. y\n"))
    case("code block escapes < & >", lambda: "<pre><code>if a &lt; b &amp;&amp; c &gt; d\n  x</code></pre>" in
         b("# T\n\n```\nif a < b && c > d\n  x\n```\n"))
    case("inline code inside bold, italic, link", lambda: (lambda h: "<strong>use <code>a&lt;b</code> now</strong>" in h
         and "<em>soft</em>" in h and '<a href="https://example.org/x?a=1&amp;b=2" rel="noopener">site</a>' in h
         )(b("# T\n\n**use `a<b` now** and *soft* and [site](https://example.org/x?a=1&b=2)\n")))
    case("relative .md link kept as text with target", lambda: "see the doc (<code>docs/OTHER.md</code>)" in
         b("# T\n\nsee [the doc](docs/OTHER.md)\n") and raises("# T\n\n[x](file.exe)\n", "unsupported link", 3))
    case("text is HTML-escaped", lambda: "a &lt; b &amp; &quot;q&quot;" in b('# T\n\na < b & "q"\n'))
    case("table with header separator", lambda: (lambda h: "<th>A</th>" in h and "<td>1 | x</td>" in h
         and h.count("<tr>") == 3 and "<td>2</td>" in h)(b("# T\n\n| A | B |\n|---|---|\n| 1 \\| x | 2 |\n| 3 | 4 |\n")))
    case("table errors (no separator, cell count)", lambda: raises("# T\n\n| A | B |\nplain\n", "separator", 3)
         and raises("# T\n\n| A | B |\n|---|---|\n| 1 |\n", "cells", 5))
    case("note block", lambda: "<blockquote>\n<p>Careful here</p>\n<p>Second</p>\n</blockquote>" in
         b("# T\n\n> Careful\n> here\n>\n> Second\n"))
    case("image inlined, prolog stripped, ids prefixed per image", lambda: (lambda h:
         "<?xml" not in h and "<!DOCTYPE" not in h and 'role="img" aria-label="Pic A"' in h
         and 'width="100%"' in h and 'max-width:720px' in h and 'viewBox="0 0 10 10"' in h
         and 'width="400"' not in h and 'id="img1-g"' in h and 'url(#img1-g)' in h and 'href="#img1-r"' in h
         and 'id="img2-g"' in h and 'url(#img2-g)' in h and "<figcaption>Pic A</figcaption>" in h
         )(b("# T\n\n![Pic A](assets/kit-guide-a.svg)\n\n![Pic B](assets/kit-guide-b.svg)\n")))
    case("missing image is an error with line", lambda: raises("# T\n\n![x](assets/kit-guide-none.svg)\n", "not found", 3))
    case("image outside assets/kit-guide- or inline is an error", lambda:
         raises("# T\n\n![x](pics/a.png)\n", "image must be", 3) and raises("# T\n\ntext ![x](assets/kit-guide-a.svg)\n", "alone", 3))
    case("unsupported syntax gives the line number", lambda: raises("# T\n\ntext\n\n* star\n", "list marker", 5)
         and raises("# T\n\n---\n", "horizontal", 3) and raises("# T\n\n<div>x</div>\n", "raw HTML", 3)
         and raises("# T\n\n##### deep\n", "heading", 3) and raises("# T\n\n```\nopen\n", "not closed", 3))
    case("list indent of 4 is an error", lambda: raises("# T\n\n- a\n    - b\n", "indent", 4))
    case("check mode: equal and unequal", lambda: compare("a\nb\n", "a\nb\n")[0] == 0
         and compare("a\nX\n", "a\nb\n")[0] == 1 and "line 2" in compare("a\nX\n", "a\nb\n")[1][0]
         and compare(None, "a")[0] == 1)
    case("determinism of two builds", lambda: b("# T\n\n## A\n\n![p](assets/kit-guide-a.svg)\n\n- x\n") ==
         b("# T\n\n## A\n\n![p](assets/kit-guide-a.svg)\n\n- x\n"))
    fails = results.count(False)
    print("creator_kit_guide selftest: %d cases, %d failed" % (len(results), fails))
    return 0 if fails == 0 else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description="Creator Kit guide: Markdown + SVG -> one HTML file")
    sub = ap.add_subparsers(dest="cmd")
    for name in ("build", "check"):
        p = sub.add_parser(name)
        p.add_argument("--md", default=DEFAULT_MD)
        p.add_argument("--out", default=DEFAULT_OUT)
    sub.add_parser("selftest")
    args = ap.parse_args(argv)
    if args.cmd is None:
        ap.print_usage(sys.stderr)
        return 2
    try:
        if args.cmd == "selftest":
            return selftest()
        if args.cmd == "build":
            return cmd_build(args.md, args.out)
        return cmd_check(args.md, args.out)
    except GuideError as e:
        where = os.path.basename(args.md) + (":%d" % e.line if e.line else "")
        print("creator_kit_guide: error: %s: %s" % (where, e.msg), file=sys.stderr)
        return 2
    except OSError as e:
        print("creator_kit_guide: error: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
