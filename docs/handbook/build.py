"""Build the offline manual from this MidiLooper checkout. Standard library only."""

from html import escape, unescape
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import subprocess
from functools import cache
from string import Template

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
DIST = HERE / "dist"
TEMPLATE = Template((HERE / "layout.html").read_text())
REPO_PATTERN = re.compile(r'<a href="REPO/([^"#]+)#L(\d+)">([^<]+)</a>')
HEADING = re.compile(r'<h2 id="([^"]+)">(.*?)</h2>', re.S)
CHECKED_REVISION = "c8851d2a4613b0e2aff83050e1feb80fa3ca0623"

# slug, navigation title, icon, heading, introductory paragraph
GROUPS = [
    ("Getting started", [
        ("index", "Overview", "book", "Overview", "An illustrated player's guide to the Teensy 4.1 MIDI looper."),
        ("setup", "Hardware & setup", "plug", "Hardware & setup", "Connect a keyboard, controller and sound source. Check the MIDI route and prepare the card before your first take."),
        ("first-recording", "Your first loop", "record", "Record your first loop", "Record a loop into an empty slot, finish the take on a downbeat and listen to it repeat."),
    ]),
    ("Playing & editing", [
        ("overdub-undo", "Overdub & undo", "layers", "Overdub & undo", "Try a second part over a playing loop. Step back through that take or return to an earlier version of the selected slot."),
        ("switching", "Switching loops", "switch", "Switching loops", "Switch to a variation at the end of a loop, or launch it on the next sixteenth note while the other tracks play."),
        ("editing", "Notes & loop ranges", "sliders", "Edit notes & loop ranges", "Move a note, adjust its velocity or repeat a few bars from a longer loop. The original bars stay available inside the slot."),
        ("groove", "Groove & quantize", "wave", "Groove & quantize", "Compare two groove templates with your original timing. Adjust the amount of swing or quantization while the loop plays."),
    ]),
    ("Library & controls", [
        ("library", "Save & load sets", "folder", "Save & load sets", "Autosave keeps the current set on the card. Save a milestone to recall all eight tracks together or load a single loop from an earlier take."),
        ("midi-controls", "MIDI control map", "faders", "Map your MIDI controller", "Assign MIDI notes to pads and Control Change or Pitch Bend messages to faders. Use MIDI feedback from the looper to drive the controller’s LEDs and motorized faders."),
        ("screens", "Screen guide", "screen", "Read the OLED", "Follow the playhead, check which slot you are editing and see when the looper has saved your take."),
    ]),
    ("Reference", [
        ("architecture", "How it fits together", "cpu", "How the firmware fits together", "Follow MIDI from the controller and the keyboard through the engine to the outputs, and one played note until it plays back."),
        ("troubleshooting", "Troubleshooting", "tool", "Troubleshooting", "Use the display and one known MIDI message to narrow down a silent loop, a waiting take or an unexpected button action."),
        ("implementation-notes", "Implementation notes", "code", "Implementation notes", "Where the firmware, DROID patch and written controls differ, what you can do on the instrument today, and what could be improved."),
    ]),
]
PAGES = [(group, *page) for group, pages in GROUPS for page in pages]

# Source links work offline. REPO links in article fragments use the same resolver.
REFERENCES = {
    "index": [("Project intent", "docs/Authority/PROJECT_INTENT.md", 13), ("Engine configuration", "include/ContentEngine/EngineConfig.h", 73)],
    "setup": [("Hardware setup", "src/main.cpp", 168), ("MIDI playback outputs", "src/MidiPorts.h", 123), ("OLED pins", "lib/SSD1322_OLED/include/SSD1322_Config.h", 9), ("Engine build", "platformio.ini", 53)],
    "first-recording": [("Record action", "include/ContentEngine/ControlSurface/ControlSurface.h", 923), ("Ending a take on a bar line", "include/ContentEngine/Playback/TrackControl.h", 98)],
    "overdub-undo": [("Undo gesture", "include/ContentEngine/ControlSurface/ControlSurface.h", 673), ("Undo depth", "include/ContentEngine/History/SlotUndoHistory.h", 326), ("Card-backed history", "docs/Guides/ENGINE_STORAGE.md", 98)],
    "switching": [("Slot launch", "include/ContentEngine/ControlSurface/ControlSurface.h", 947), ("Launch setting", "src/SerialCommands.h", 316)],
    "editing": [("Edit gesture", "include/ContentEngine/ControlSurface/ControlSurface.h", 707), ("Step editing", "include/ContentEngine/ControlSurface/ControlSurfaceNoteEdit.h", 460), ("Range selection", "include/ContentEngine/ControlSurface/ControlSurfaceLoopEdit.h", 180)],
    "groove": [("Groove faders", "include/ContentEngine/ControlSurface/ControlSurfaceGroove.h", 342), ("Groove controls", "include/ContentEngine/ControlSurface/ControlSurface.h", 653)],
    "library": [("Browser rows and controls", "include/ContentEngine/ControlSurface/SetBrowser.h", 10), ("Snapshot contents", "include/ContentEngine/Storage/LibraryWriter.h", 35), ("Card format", "docs/Guides/ENGINE_STORAGE.md", 98)],
    "midi-controls": [("Button decoder", "include/ContentEngine/ControlSurface/ControlSurface.h", 204), ("MIDI ports", "src/MidiInput.h", 96)],
    "screens": [("OLED layout", "include/Teensy/OledScreen.h", 14), ("Framebuffer export", "src/SerialCommands.h", 251), ("Save indicators", "include/ContentEngine/Display/StatusView.h", 28)],
    "architecture": [],
    "troubleshooting": [("Thru and mute", "include/ContentEngine/Playback/ClockOutput.h", 17), ("Card setup at boot", "src/main.cpp", 208)],
    "implementation-notes": [],
}

ICONS = {
    "book": '<path d="M3 4h6a4 4 0 013 2 4 4 0 013-2h6v14h-6a4 4 0 00-3 2 4 4 0 00-3-2H3zM12 6v14"/>',
    "plug": '<path d="m7 14 5-5 4 4-5 5H7zM13 8l3-3m1 7 3-3M7 18l-3 3"/>',
    "record": '<circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="3" fill="currentColor"/>',
    "layers": '<path d="m3 8 9-5 9 5-9 5zM3 12l9 5 9-5M3 16l9 5 9-5"/>',
    "switch": '<path d="M3 7h16m-4-4 4 4-4 4M21 17H5m4-4-4 4 4 4"/>',
    "sliders": '<path d="M3 7h6m5 0h7M3 17h11m5 0h2"/><circle cx="11.5" cy="7" r="2.5"/><circle cx="16.5" cy="17" r="2.5"/>',
    "wave": '<path d="M2 12h3l3-7 4 14 4-14 3 7h3"/>',
    "folder": '<path d="M3 6h6l3 3h9v10H3z"/>',
    "faders": '<path d="M5 3v5m0 4v9m7-18v11m0 4v3m7-18v3m0 4v11M3 9h4m3 6h4m3-8h4"/>',
    "screen": '<rect x="2.5" y="4" width="19" height="14" rx="2"/><path d="M8 21h8M7 12h3l2-3 3 5 2-2"/>',
    "tool": '<path d="M15 3a5 5 0 00-6 6l-6 6a3 3 0 004 4l6-6a5 5 0 006-6l-3 3-3-3z"/>',
    "code": '<path d="m8 6-6 6 6 6m8-12 6 6-6 6M14 4l-4 16"/>',
    "menu": '<path d="M3 6h18M3 12h18M3 18h18"/>',
    "search": '<circle cx="10" cy="10" r="6.5"/><path d="m15 15 6 6"/>',
    "external": '<path d="M6 17 18 5M7 5h11v11"/>',
    "cpu": '<rect x="5" y="5" width="14" height="14" rx="2"/><path d="M9 2v3m6-3v3m-6 14v3m6-3v3M2 9h3m-3 6h3m14-6h3m-3 6h3"/><rect x="9" y="9" width="6" height="6"/>',
    "mark": '<path stroke-width="2.8" d="M2 10v4m5-8v12m5-16v20m5-17v14m5-10v6"/>',
    "github": '<path fill="currentColor" stroke="none" d="M12 .9A11.1 11.1 0 008.5 22.5c.55.1.76-.24.76-.54v-2.1c-3.1.67-3.76-1.32-3.76-1.32-.51-1.28-1.24-1.62-1.24-1.62-1.01-.69.08-.68.08-.68 1.12.08 1.7 1.15 1.7 1.15.99 1.7 2.6 1.21 3.24.93.1-.72.4-1.2.71-1.48-2.47-.28-5.07-1.24-5.07-5.5 0-1.21.43-2.2 1.14-2.97-.12-.28-.5-1.4.11-2.93 0 0 .93-.3 3.05 1.13a10.6 10.6 0 015.55 0c2.12-1.43 3.04-1.13 3.04-1.13.62 1.53.24 2.65.12 2.93.72.77 1.14 1.76 1.14 2.97 0 4.27-2.6 5.22-5.08 5.5.4.35.76 1.02.76 2.06v3.06c0 .3.2.65.76.54A11.1 11.1 0 0012 .9z"/>',
}

def icon(name, extra=""):
    return f'<svg class="ico {extra}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">{ICONS[name]}</svg>'


class Words(HTMLParser):
    def __init__(self):
        super().__init__()
        self.text = []

    def handle_data(self, data):
        self.text.append(data)


def plain(markup):
    reader = Words()
    reader.feed(markup)
    return " ".join(" ".join(reader.text).split())


def navigation(current):
    groups = []
    for group, pages in GROUPS:
        links = []
        for slug, name, symbol, _, _ in pages:
            active = ' aria-current="page"' if slug == current else ''
            links.append(f'<a class="nav-item" href="{slug}.html"{active}>{icon(symbol)}<span>{escape(name)}</span></a>')
        groups.append(f'<div class="nav-group"><h2>{group}</h2>{"".join(links)}</div>')
    return "".join(groups)


def page(slug, title, description, body, pageclass):
    crumb = "Source excerpts" if slug == "sources" else next(row[2] for row in PAGES if row[1] == slug)
    output = TEMPLATE.substitute(
        title=escape(title), description=escape(description, quote=True), navigation=navigation(slug),
        pageclass=pageclass, content=body, breadcrumb=escape(crumb),
        menu_icon=icon("menu"), wave_icon=icon("mark"), cpu_icon=icon("cpu"),
        github_icon=icon("github"), external_icon=icon("external", "mini"), search_icon=icon("search"),
    )
    (DIST / f"{slug}.html").write_text(output)


def section_header(group, title, description):
    return f'<header class="article-heading"><p class="eyebrow">{escape(group)}</p><h1>{escape(title)}</h1><p class="lede">{escape(description)}</p></header>'


@cache
def source_bytes(file):
    # Keep excerpts and the downloadable patch aligned to the reviewed firmware.
    folder = ROOT
    revision = CHECKED_REVISION
    if file.startswith("lib/SSD1322_OLED/"):
        module = "lib/SSD1322_OLED"
        folder = ROOT / module
        revision = subprocess.check_output(["git", "rev-parse", f"{CHECKED_REVISION}:{module}"], cwd=ROOT, text=True).strip()
        file = file.removeprefix(module + "/")
    return subprocess.check_output(["git", "show", f"{revision}:{file}"], cwd=folder)


def main():
    references = {}
    index = []
    for filename, original in (("midilooper_v1.ini", "droid/midilooper_v1.ini"), ("LICENSE.txt", "LICENSE")):
        (DIST / "assets" / filename).write_bytes(source_bytes(original))

    def source_link(label, file, line):
        tag = re.sub(r'[^\w-]', '-', file) + f'-L{line}'
        references.setdefault(tag, (label, file, line))
        return f'<a href="sources.html#{tag}">{escape(label)}</a>'

    for position, (group, slug, nav, _, title, desc) in enumerate(PAGES):
        article = (HERE / "pages" / f"{slug}.html").read_text()
        article = REPO_PATTERN.sub(lambda m: source_link(unescape(m[3]), m[1], int(m[2])), article)
        if 'href="REPO/' in article:
            raise ValueError(f"Unresolved firmware reference in {slug}")
        article = article.replace('<table>', '<div class="table-scroll"><table>').replace('</table>', '</table></div>')
        headings = list(HEADING.finditer(article))
        index.append(dict(url=f"{slug}.html", title=title, chapter=nav, group=group, top=True, body=desc))
        for i, heading in enumerate(headings):
            end = headings[i + 1].start() if i + 1 < len(headings) else len(article)
            index.append(dict(url=f"{slug}.html#{heading[1]}", title=plain(heading[2]), chapter=nav,
                              group=group, top=False, body=plain(article[heading.end():end])))
        refs = " · ".join(source_link(*r) for r in REFERENCES[slug])
        if refs:
            refs = f'<div class="sources-row"><p class="eyebrow">Source notes</p>{refs}</div>'
        if slug == "index":
            page(slug, title, desc, article + refs, "home")
            continue
        outline = "".join(f'<a href="#{h[1]}">{h[2]}</a>' for h in headings)
        paging = []
        for offset, label in ((-1, "Previous"), (1, "Next")):
            adj = position + offset
            if 0 <= adj < len(PAGES):
                entry = PAGES[adj]
                paging.append(f'<a href="{entry[1]}.html"><small>{label}</small>{escape(entry[2])}</a>')
        body = (f'<div class="article-grid"><article class="article-main">{section_header(group,title,desc)}'
                f'<div class="article-body">{article}</div>{refs}<nav class="next-links" aria-label="Adjacent chapters">{"".join(paging)}</nav>'
                f'</article><nav class="outline" aria-label="On this page"><p class="outline-title">On this page</p>{outline}</nav></div>')
        page(slug, title, desc, body, "chapter")
    (DIST / "assets/search-index.js").write_text("window.manualIndex = " + json.dumps(index, ensure_ascii=False) + ";\n")
    evidence = []
    for tag, (label, file, line) in references.items():
        lines = source_bytes(file).decode().splitlines()
        if not 1 <= line <= len(lines):
            raise ValueError(f"Invalid source reference {file}:{line}")
        first = max(1, line - 1)
        excerpt = "\n".join(f"{i:>4}  {lines[i-1]}".rstrip() for i in range(first, min(line + 10, len(lines)) + 1))
        evidence.append(f'<section class="evidence" id="{tag}"><h3>{escape(label)}</h3><p><code>{escape(file)} · line {line}</code></p>'
                        f'<details><summary>Read the excerpt</summary><pre><code>{escape(excerpt)}</code></pre></details></section>')
    title = "Source excerpts"
    desc = f"Selected passages from the reviewed engine checkout ({CHECKED_REVISION[:8]}, 12 November 2026). Paths and line numbers locate the implementation in the repository."
    page("sources", title, desc, '<article class="sources-index">' + section_header("Reference", title, desc) + "".join(evidence) + '</article>', "chapter")


if __name__ == "__main__":
    main()
