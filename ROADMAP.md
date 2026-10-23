# Roadmap: Scaredy Cat

Duck-style volume shaper.

## Next
- [ ] Per band spectrum at each band frequency (as in the mockup)
- [ ] Curved EQ points
- [x] DSP and plugin shell on cat-ui (core, editor, installers, manual)

## Every cat plugin
- Draw through `catui::RenderGate` so meters and animations stop when the window is not in front (audio, automation and MIDI keep running)
- Settings page with MIDI filters on by default, selectable preset/backup folders, UI size reset to 100%
