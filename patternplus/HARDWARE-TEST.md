# Pattern+ couch sheet and tests

The grid, pages and basic chain playback have passed on the device. This build (0.5.1) adds stock's live edit: Shift + left / right while playing frees the cursor from the playhead. 0.5.0 made the cursor walk along the chain, and added Help as a key you hold to keep the cursor and HOLD on one page. Cutting, copying and pasting patterns between pages, with undo, came in 0.4. Saving is automatic and comes back after a power off; saved collections are a separate list you can name, load and start over from.

## Before you trust it with a good idea

Saving hasn't been tried on the device yet, so run the **Saving test** below first.

## Build

From the repository root, in Git Bash or WSL:

```bash
bash tools/build.sh patternplus
```

Host test of the logic:

```bash
gcc -Wall -I patternplus/plugin patternplus/plugin/test/host_test.c patternplus/plugin/pp_player.c patternplus/plugin/pp_store.c -o /tmp/pp_test && /tmp/pp_test
```

## Install

Open the Hundekraft workshop, go to **Your plugins**, add `build/patternplus.knl`, then review and send it. It replaces the earlier build. A harmless `ppluschk` database table from the first checkpoint stays on the device.

## Controls on the Pattern+ screen

| Control | Action | Source |
| --- | --- | --- |
| Note key | Sounds the note and moves the left arrow | Stock |
| Shift + note key | Adds a note. Stopped: at the cursor, then the cursor moves on. Playing: at the playhead. If that note is already there, erases it and the cursor stays | Stock, except erasing |
| Left / right, or blue turn | Moves the cursor while stopped. With a chain, going past the end of a page's section moves on to the next page in the chain, and past its start back to the previous one, wrapping around | Stock, except the chain |
| Shift + left / right, while playing | Live edit: the cursor leaves the playhead and moves one step. After that, left / right, blue, Shift + note key and Shift + blue work at the cursor while the pattern plays. With a chain, the grid stays on this page and `FREE` shows | Stock |
| Help, held | Keeps the cursor on this page and makes HOLD loop just one page. Shows `PAGE`. Letting go returns to the chain | New |
| Shift + blue turn | Moves the cursor while stopped, clearing each column it leaves | Stock |
| Green turn | Swing. The number stays bright while it is anything but 50% | Stock |
| Shift + green turn | Rotates the notes inside the section | Stock |
| White turn | Section length | Stock |
| Shift + white turn | Moves the section | Stock |
| Orange turn | Right = HOLD on and play, left = HOLD off | Stock |
| Shift + orange turn | Direction | Stock |
| Shift + blue push | Previous page | New |
| Shift + green push | Next page, or a new blank page after the last one | New |
| Shift + white push | Next view: grid, chain, saved, then grid again. Playback isn't disturbed | New |
| Shift + orange push | Playing: makes the grid follow the playing page again and ends live edit. Stopped: undoes the last cut or paste, up to 3 | New |
| Lift | Stopped: copies this page's pattern: its notes, section, swing and direction | New |
| Drop | Stopped: pastes the pattern onto this page. Asks first if the page already has notes | New |
| Shift + drop | Stopped: pastes the pattern onto a new last page | New |
| Scissors | Stopped: cuts this page's pattern, leaving the page blank. Pages never move, and the chain never changes | New |
| Green push / orange push, while asked | Green confirms, orange cancels | New |

In the chain view, a page with no notes on it shows in red. It plays a bar of silence at its own length.

In the chain view:

| Control | Action |
| --- | --- |
| Blue turn, or left / right | Moves the underline along the chain |
| Green turn | Picks a page. On an existing entry, this changes that entry |
| Green push | Adds the picked page at the underline |
| White push | Removes the entry at the underline |
| Orange turn | HOLD, as in the grid |

In the saved view:

| Control | Action |
| --- | --- |
| Blue turn, or left / right | Moves the arrow along slots 1 to 8 |
| White push | Saves all pages and the chain to this slot, then asks for a name. Blue and green pick a letter, white push adds it, left deletes, orange push finishes. Pressing a mode key cancels |
| Green push | Asks before loading this slot, which replaces your current work and stops playback |
| Shift + green push | Asks before clearing your work: one empty page, no chain. Saved slots are untouched |
| Green push / orange push, while asked | Green confirms, orange cancels. The question stays until you answer, move the arrow or leave the view |
| Orange turn | Does nothing. Anything playing keeps playing |

Saving onto a used slot replaces it after you finish the name. Synth and Drum each have their own 8 slots. Green text marks the slot you last loaded or saved.

If the automatic save can't be read at power on, `pattern+ save unreadable, copy kept` appears, and Pattern+ starts with an empty page. The unreadable save is copied to `patternplus/unreadable-s.txt` (Synth) or `unreadable-d.txt` (Drum) before anything is saved over it. You can't easily cause this on purpose, so there's no test step for it.

The grid's other controls, including Shift + note key, do nothing in the chain and saved views.

On the Synth, Drum, Tape and Mixer screens, holding a note key plays the page transposed, or changes the transposition if HOLD is on.

## Differences from stock in this build

1. **Lift, drop and scissors** copy, paste and cut patterns between pages instead of stock's own uses.
2. **Left / right and blue while playing** do nothing until you start live edit with Shift + left / right. Stock shows the cursor fighting the playhead; I left that out.
3. **Shift + blue while playing** does nothing until you start live edit.
4. **Help** on the Pattern+ screen is the hold-for-one-page key, so stock's help text doesn't show there. Shift + help still works.
5. **Font:** the swing, HOLD and page labels use Hundekraft's stroke font, so they won't match stock exactly.

## Chain behavior, provisional

1. A chain with entries in it always plays from its first entry when you start. An empty chain loops the selected page, as before.
2. Pages change when a page finishes its cycle, including its direction and section. Repeats of a page share its notes, so editing B changes every B.
3. The grid follows the playing page. Changing page by hand during chain playback stops that and shows `FREE`, and the cursor works on any page that isn't playing. Shift + orange push, or starting playback again, resumes following.
4. Limits: 16 pages (A to P), 32 chain entries, one chain per mode.
5. Holding Help when you start HOLD plays the page on screen instead of the first entry. After you let go, the chain carries on from that page.

## Chain cursor and Help test (0.5.0)

In Synth mode, select `patternplus`, with nothing playing.

1. Make pages A, B, C and D. Put a different note on the first step of each. Give B a section of 4 steps with the white encoder.
2. Build the chain `A > B > A > C` in the chain view, then come back to the grid on A.
3. Turn blue right slowly. After A's last step the grid shows B with the cursor on its first step. After B's 4th step it shows A again, then C, then A once more. Turning left goes back through the same pages in reverse.
4. Go to D with Shift + green push. Turn blue right past its last step. The cursor goes back to D's first step, because D isn't in the chain.
5. Go back to A. Hold Help: `PAGE` shows under the page letter. Turn blue right past A's last step. The cursor goes back to A's first step. Let go of Help and `PAGE` goes away.
6. Turn orange right. The chain plays A, B, A, C. While A is playing, hold Help. A repeats for as long as you hold it. Let go partway through A: A finishes, then B plays. No notes should keep ringing.
7. Turn orange left to stop. Go to C, hold Help, and turn orange right. C repeats. Let go: C finishes, then A plays, then the chain carries on as usual.

## Live edit test

Not yet tried on the device. In Synth mode, select `patternplus`, with one page, no chain and a few notes on it.

1. Turn orange right. The white playhead moves across the grid.
2. Press left, then turn blue. Nothing happens.
3. Hold Shift and press right once. A second, pale blue line appears one step right of where the playhead was, and stays there while the playhead keeps moving. Does the pattern keep playing in time?
4. Let go of Shift. Press left and right, and turn blue. The pale blue line moves; the playhead isn't affected.
5. Hold Shift and press a note key. A dot appears at the pale blue line, not at the playhead, and the line moves on one step. You hear it next time the playhead passes.
6. Hold Shift and turn blue left two steps. The line passes back over the dot and erases it.
7. Press Shift + orange push. The pale blue line goes away. Hold Shift and press a note key: the dot lands at the playhead again.
8. Do step 3 again, then turn orange left and right. The pale blue line is gone and the cursor is back on the playhead.
9. On stock Pattern, do steps 1 to 6. Tell me anything that differs, and how stock leaves live edit.
10. Build the chain `A > B` and play it. While A plays, hold Shift and press right. `FREE` shows, the grid stays on A while B plays, and the pale blue line is still free when A comes round. Shift + orange push follows the chain again.

## Saving test

**Automatic**

1. Enter some notes on a page, make a second page, and build a short chain.
2. Turn the OP-1 off and on, then select `patternplus`. Your pages and chain are back, on the page you were on, and nothing is playing. Waiting a few seconds before powering off gives the background save time to finish. Tell me if a quick power off loses anything.
3. Do the same in Drum mode. Synth and Drum keep separate work.

**Collections**

4. Press Shift + white push twice to reach `SAVED`. Slots read `EMPTY`.
5. Press white on slot 1, enter a name, and push orange to finish. `SAVED` shows and the name appears in green.
6. Press Shift + green push. `CLEAR ALL? GREEN YES ORANGE NO` appears and stays. Push orange: `CANCELLED`, and your work is untouched. Press Shift + green push again, then push green: `NEW`, and your work clears. Press Shift + white push once to see the empty grid.
7. Enter different notes. Back in `SAVED`, turn blue to slot 2 and save it under another name.
8. Turn blue to slot 1, push green, and read the question: `REPLACE WORK? GREEN YES ORANGE NO`. Push green. Check the grid: slot 1's work is back. Do the same for slot 2.
9. Turn the OP-1 off and on. Both names are still there, and each loads correctly.
10. Start a save, then press a mode key instead of finishing the name. Nothing changes.
11. Load a slot while a pattern plays. It stops with no stuck notes.

## Cut, copy and paste test

In Drum mode, select `patternplus`, with nothing playing. The message line is under the grid.

1. Make pages A, B, C and D with Shift + green push. Give each a different beat, and give D a shorter section and a different swing.
2. Build the chain `A > B > C > D > C > B > A` in the chain view, then come back to the grid.
3. On D, press scissors. `CUT D` shows and D's grid is empty. Open the chain view: every D is red, and the chain is otherwise unchanged. Come back to the grid.
4. Go to B with Shift + blue push and press drop. `PASTE OVER B? GREEN YES ORANGE NO` shows and stays. Push orange: `CANCELLED`, and B is unchanged.
5. Press drop again and push green: `PASTED ON B`. B now has D's old beat, section and swing. C is unchanged.
6. Go to D and press drop. It pastes straight away, with no question, because D was blank. The chain view's Ds are no longer red.
7. Press Shift + orange push three times: `UNDONE` each time. D is blank again, then B's own beat is back, then D's beat is back.
8. Press Shift + orange push again: `NOTHING TO UNDO`.
9. On C, press lift: `COPIED C`. Press Shift + drop: `PASTED ON NEW E`, and you're on E with C's beat. Press Shift + orange push: E is gone, and the page counter reads `/D` again.
10. Start the pattern and press lift: `STOP FIRST`. Stop it.
11. Switch to Synth mode and press drop: `NOTHING COPIED`. Synth and Drum each have their own clipboard and undo.
12. Back in Drum mode, cut a page that's in the chain and play the chain. That page plays a bar of silence.

## Chain test

Note what happens if a step doesn't match. On a fresh start, in Synth or Drum mode, select `patternplus`.

**Build `A > B > B > C > D`**

1. Make four pages with different notes. On A, hold Shift and press a key or two. Press Shift + green push for B and add different notes. Do the same for C and D.
2. Press Shift + white push. The screen shows `CHAIN`, an underlined letter, and `NO CHAIN  PAGE LOOPS`.
3. Turn green left until the letter is A, then press green. `A` appears and the underline moves on.
4. Turn green right to B and press green twice. Turn green to C and press green. Turn green to D and press green. The chain reads `A > B > B > C > D`. Is everything readable and in the right place? A mirror screenshot helps most.

**Play it**

5. Turn orange right. HOLD lights and a green box sits on `A`, then moves along the chain and back to the start. Do you hear both Bs in full, in order? Does the box stop on each B separately?
6. Press Shift + white push while it plays. The grid appears and shows each page as it plays, with the playhead, and the letter at lower right changes. Does the timing stay steady through the switch? Switch back and forth a few times.

**Editing while playing**

7. In the grid, press Shift + blue push or Shift + green push to go to a page. `FREE` appears under the letter and the grid stays put while the music moves on. When that page isn't playing, turn blue and add a note with Shift + key: it goes at the cursor.
8. Press Shift + orange push. `FREE` goes away and the grid goes back to the playing page. Tell me whether this behavior feels right, and whether `FREE` is clear enough.
9. Back in the chain view, turn blue to the first `B`, then turn green to change it to D. The change is heard next time around. Press white to remove it. Removing the entry that's playing lets it finish, then the next one plays.
10. Turn orange left to stop, then right to start. Playback starts from the first entry.

**Nothing gets stuck**

11. No note should keep ringing after each of these while the chain plays:
    1. Turn HOLD off.
    2. Press the sequencer on/off key, then press it again. Where does it carry on from?
    3. Switch between Synth and Drum. The other mode has its own pages and chain.
    4. Pick another sequencer in the selector.
12. Remove every entry from the chain. The current page should just loop, as before.

**Erasing with Shift + note**

13. Stopped: hold Shift and press a key, turn blue left one step, then hold Shift and press the same key. The dot disappears and the cursor stays on that column.
14. Hold Shift and press two different keys on one column, turning blue left one step between them. Turn blue left one step and erase one of them. The other stays.
15. Turn orange right. While it plays, hold Shift and press a key: a dot appears at the playhead. When the playhead comes back to that column, press the same key again with Shift. The dot disappears. Does this feel right?

**Also new in this build**

16. Drums use stock's pitch. Notes now last the whole step and play at stock's velocity. On a synth with a long sound, compare a pattern against stock Pattern by ear: does anything sound different?

## Grid and pages test

These steps passed on the earlier build.

**Look**

1. Select `patternplus` in the sequencer selector. The screen looks like stock Pattern, with a large `A/A` below the direction arrow.
2. Compare it with stock Pattern. Is anything out of place, or a wrong color or size? A mirror screenshot of the Pattern+ screen would help most.
3. Is the page letter easy to read, and does it stay clear of everything else?

**Same as stock**

4. Press a note key without Shift. The note sounds, the left arrow moves, and nothing starts playing.
5. Hold Shift and press four different keys. Each adds a dot and the cursor moves on. Do the keys sound while you enter them, as they do on stock?
6. Turn orange right. HOLD lights, the playhead moves, and you hear your notes. Compare with stock Pattern by ear, and tell me if any of these differ:
   1. Tempo: the time one step takes.
   2. Note length: how long each note rings.
   3. Loudness.
7. While it plays, try swing, rotate, length, moved section and all three directions. Each should match stock.
8. Turn green all the way both ways. It stops at 20% and 80%, matching stock.
9. Press tape Stop. The pattern keeps playing, as on stock. Turn orange left: it stops.

**Transposing**

The TE guide (section 7.6, "Play and hold a sequence") says to switch to Synth, Drum, Tape or Mixer mode and press a key to transpose, and that the C key plays the original pitch. It doesn't say which C, and the keyboard has two.

10. Select stock Pattern and enter one note: hold Shift and press the lower C (the left one). Press the Synth key to leave the Pattern screen, then hold the lower C. Is the pitch the same as when you entered it, or an octave off?
11. Without leaving the Synth screen, hold the upper C (the right one). Same pitch, or an octave off? Whichever C matched is stock's original-pitch key.
12. Repeat steps 10 and 11 on Pattern+. Pattern+ should use the same C as stock.
13. Select stock Pattern and enter a note. On the Synth screen, press the right arrow key once to shift the keyboard up an octave, then hold the C from step 10 or 11. Does the sequence play an octave higher? Repeat on Pattern+.

**Pages**

14. With notes on A, press Shift + green push. The label reads `B/B` and the grid is blank.
15. Enter different notes on B, then press Shift + blue push. The label reads `A/B` and A's notes are intact.
16. Press Shift + blue push again on A. It stays on A.
17. Press Shift + green push to return to B. No new page appears.
18. Keep pressing Shift + green push until `H/H`, then once more. `FULL` shows briefly and nothing else changes.
19. Set a different length or swing on two pages, then switch between them. Each keeps its own settings.
20. Play A with HOLD on and press Shift + green push. The grid shows B at once and the sound moves to B when A finishes its loop. Try playing and switching a few times, and tell me whether this feels right.

**Nothing gets stuck**

21. No note should keep ringing after each of these while playing:
   1. Turn HOLD off.
   2. Press the sequencer on/off key. Press it again: does playback carry on from where you'd expect?
   3. Switch between Synth and Drum.
   4. Pick another sequencer in the selector.

**Drum mode**

22. In Drum mode, select `patternplus`, enter notes and play them. It should keep separate pages from Synth mode. Does the grid behave like stock's Drum grid?
23. On stock, how many notes will one column take? Try adding them on Pattern+ too.
