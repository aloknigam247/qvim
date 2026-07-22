---
name: visual-validate-qvim
description: >
  Use after any change to the paint path (GridItem, CursorItem), font handling, glyph rendering
  (PUA / nerd-font workaround, ligatures), cursor shape or animation, highlight resolution, or
  selection rendering — anywhere ctest can pass while the user-visible feature is broken. Takes a
  screenshot of qvim with a known cursor/font/colour config and inspects the rendered window. Per
  the project's "Treat agent reports as claims, not proof" rule, this is the authoritative
  validation gate for visual changes — never claim a visual change works without running this skill.
metadata:
  category: development
  agent_type: general-purpose
keep-coding-instructions: true
---

# Visual validation for qvim

ctest verifies code correctness. This skill verifies feature correctness — what the user actually sees on screen. Run it whenever you touched the paint path, fonts, cursor, or anything that affects the rendered window.

## When to run

- After editing `src/GridItem.cpp` or `src/CursorItem.cpp` (paint path).
- After editing `include/CursorItem.h` (animation timing, blink params).
- After changing `src/CellMetrics.cpp` or font / linespace handling.
- After changing colour resolution in `src/HighlightTable.cpp`.
- After changing `qml/Shell.qml` z-order or item composition.
- After any change where you'd be tempted to write "should render correctly" — don't write it, run this instead.

## Steps

### 1. Force-rebuild the qvim binary

The incremental linker skips relinking `qvim.exe` when only the .lib changed (see project `AGENTS.md` gotcha). A stale exe means you're validating a previous version. Always run:

```pwsh
cmake --build --preset dev --target qvim
```

If you're validating an agent's worktree branch, build inside the worktree, not the main checkout, and confirm with `Get-Item <worktree>/build/dev/Debug/qvim.exe` that the mtime is post-commit.

### 2. Generate fixture files with LF endings

PowerShell's `Set-Content` writes CRLF on Windows. nvim reads CRLF as a `^M` literal in `-u init.vim` and fails with `E488: Trailing characters`. Use `[IO.File]::WriteAllText` with explicit backtick-`n`:

```pwsh
$tmp = "$env:TEMP\qvim-visual-$([guid]::NewGuid())"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

[IO.File]::WriteAllText("$tmp\init.vim", @"
set guicursor=a:block-Cursor-blinkwait0-blinkon0-blinkoff0
"@.Replace("`r`n", "`n"))

[IO.File]::WriteAllText("$tmp\sample.txt", @"
the quick brown fox jumps over the lazy dog
THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG
0123456789 abcdefghijklmnopqrstuvwxyz
"@.Replace("`r`n", "`n"))
```

Always disable blink in the fixture (`blinkwait0-blinkon0-blinkoff0`) so the capture is deterministic — without it the cursor may be in the blink-off phase when `PrintWindow` fires and you'll think the cursor is broken when it just blinked off.

If the feature under test requires insert mode, append `autocmd VimEnter * startinsert` on its own line. For vertical bar or horizontal bar cursors, override `guicursor` with the explicit shape (`ver25-Cursor` / `hor20-Cursor`).

### 3. Take the screenshot

`scripts/screenshot-qvim.ps1` handles the SetForegroundWindow + `PrintWindow PW_RENDERFULLCONTENT` dance needed to capture Qt Quick's DirectComposition surface. Default settle is 5 seconds, which is enough for the attach + first paint.

```pwsh
$out = "$tmp\shot.png"
pwsh D:\qvim\scripts\screenshot-qvim.ps1 -File "$tmp\sample.txt" -InitFile "$tmp\init.vim" -OutPath $out
```

Use `-Exe "D:\qvim\build\release\RelWithDebInfo\qvim.exe"` if you want the release build's render (closer to user experience, faster animation). Default targets `build\dev\Debug\qvim.exe`.

If the script reports "qvim has no main window after Nms", bump `-SettleMs 8000`. If the captured image shows another window's content, the qvim window wasn't actually foregrounded — same fix, or close any blocking modal first.

### 4. View the PNG and inspect

```
view tool with path = $out
```

Check the things the change was supposed to affect. A sample checklist for cursor-path changes:

- Block cursor: visible solid rectangle at the expected cell, target glyph painted on top in defaultBg.
- Vertical cursor (ver25): ~25%-width sliver at the cell's left edge.
- Horizontal cursor (hor20): ~20%-height sliver at the cell's bottom edge.
- No "ghost" cursor in the previous position (especially after a cursor move test).
- Underlying text not corrupted by the overlay (cursor cell shows correct glyph, surrounding cells unchanged).

Do NOT trust a screenshot at col 1 row 1 alone for a vertical cursor — the thin bar overlaps the first glyph and is hard to see. Either render the cursor at a cell that's empty (`x   y` + `normal 2l`) or use an explicit high-contrast highlight (`highlight Cursor guifg=black guibg=#ff00ff`).

### 5. Capture a "before" if the change is subtle

If the change is a visual tweak (cell-percentage, antialiasing, hinting, colour shift), build the OLD binary first, screenshot, then build the NEW binary and screenshot. Compare side by side. `git stash` + rebuild gets you the old binary without losing the work.

## Anti-patterns

- Skipping step 1 — the post-build runs but a Qt-DLL change can leave the launch fixture stale.
- Using `Set-Content` for fixture files (CRLF trap).
- Capturing at col 1 row 1 with a vertical cursor — invisible.
- Capturing with blink enabled — flaky.
- "It compiles and tests pass, so it works" — for visual changes, that's not what works means.
