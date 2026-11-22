# op1inventory

A page that lists what's installed on a Hundekraft OP-1: every plugin file, whether it loaded, its version, what it
claims, and the switched-off list, which it can also change. It uses the workshop's own USB MIDI code (`op1push.js`
from a local copy of the Hundekraft workshop), unchanged, through a tiny local server. Reading changes nothing on the
OP-1; only the switched-off list's save button writes.

Made to find which plugin turns blue's page 2 off (see [pushprobe](../pushprobe/README.md)).

## Use

1. Connect the OP-1 by USB and close any workshop tab (only one page can talk to the OP-1 at a time).
2. In a terminal, from the repository root:

   ```powershell
   node research/op1inventory/serve.mjs
   ```

   It expects the workshop in `D:\src\op1-workshop`; pass another folder as the first argument if it's elsewhere.
3. Open http://localhost:8461/ in Chrome or Edge and press **Read from OP-1**. Allow MIDI (with system exclusive
   messages) when the browser asks.
4. Copy the text block at the bottom into the chat.
5. Stop the server with Ctrl+C.

## Switching plugins off

After a read, the page shows the OP-1's switched-off list in a box. Names typed there (from the Name column, separated
by commas) stay installed but don't load from the next restart. **Save on OP-1** asks first, then stores the list and
reads it back. Switch the OP-1 off and on for it to take effect; empty the box and save to switch everything back on.
