# Status pictures

`render_cnk_status.py` draws `status/cnk-progress.svg`, the progress card in
the README section "CNK Expansion – Progress", from `status/cnk.json`. It needs
Python 3 and nothing else.

## Updating the CNK progress

1. Edit `status/cnk.json`:
   - `last_updated`: the date of the update, written as `2026-10-30`
   - `tracks`: `complete` of `total` CNK tracks
   - `functions_verified`: `verified` of `in_scope` functions
   - `building_blocks`: one entry per block; `state` is `done`, `in_progress`
     or `planned`. Names have at most 48 characters
2. Regenerate the picture from the repository root:

       python tools/status/render_cnk_status.py

3. Commit `status/cnk.json` and `status/cnk-progress.svg` together. Never edit
   the SVG by hand: the next run overwrites it.

The script checks the JSON first. A missing or unknown key, an unknown state,
a count that is not a whole number or larger than its total, or a date that
does not exist stops it with exit code 2 and a message that names the entry;
nothing is written then.

    python tools/status/render_cnk_status.py --check

writes nothing and exits with 1 if the SVG is not what the JSON gives - a
quick test before a commit.

Only counts and names go into the JSON, never data from the game.
