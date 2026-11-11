# MidiLooper Manual

Open [the website](dist/index.html) from a local checkout. The complete manual, search, device screens, source excerpts and downloadable DROID patch work offline. For a local HTTP preview, run `python3 -m http.server --directory docs/handbook/dist` from the firmware repository root.

## Update the manual

Edit the article fragments in `pages/`. Page titles, navigation order, intros and firmware references live in `build.py`. Edit `layout.html` for shared markup. Styles, interaction code and images live in `dist/assets/`. Run the following from the repository root and commit the regenerated pages, source notes and search index with the edits.

```sh
python3 docs/handbook/build.py
```

The reference revision in the builder pins the excerpts and controller download to the firmware version the prose describes. Before updating it, check the gestures, card format and patch against the new revision. A link in an article of the form `REPO/path/to/file#L123` is converted into an offline source excerpt linked to that line.

## Image provenance

The hardware photographs come from the repository’s `Images/midilooper.jpg` and `Images/midilooper_droid.jpg`. They show prototype hardware. The live screen, `oled-performance.png`, is the attached Teensy’s complete 256 × 64 framebuffer from 11 November 2026, exported via `!ENGINE fbdump` and rendered at 4× with `scripts/render_display_dump.py`.

All nine `engine-*.png` images are example frames drawn on a computer with the actual engine renderer, SSD1322 graphics and device fonts using sample notes and library entries. Each example was checked against the manual’s firmware revision. Labels in the manual identify the live capture and the software-rendered examples.

The static hosting manifest is in `.openai/hosting.json`. Only the handbook belongs in its dedicated Site repository. The firmware repository and session captures are separate.
