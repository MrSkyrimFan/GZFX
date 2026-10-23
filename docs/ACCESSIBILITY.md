# Diagnostic accessibility

GZFX development diagnostics must never rely on hue alone.

Any in-game development state, warning, split comparison, or mode marker must provide at least one non-color cue such as:

- black/white luminance contrast;
- pattern or geometry;
- readable text;
- position or shape.

The split-screen comparison uses an alternating black/white center marker plus ORIGINAL / GZFX labels. Color may be used decoratively later, but it must never be the only way to identify a state.

This rule applies to developer builds and the eventual public in-game editor.
