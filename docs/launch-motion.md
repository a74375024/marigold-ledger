# Launch motion

Opening the app: the Marigold flower covers loading, lights its petals as
the channel list comes in, then blooms out while the guide comes in behind
it. "Lumen" with Unfurl's opening and the Bloom-out exit, chosen from the
launch mocks. The motion is `src/core/engine/launch_motion.*`; the drawing is
`src/app/screens/launch_mark.*`; the guide hosts it on its first draw.

The first frame is the system splash's pose (`app/splash.png`): 12 outer
circles (radius 36, 67 from the centre, #D6781E), 10 inner (radius 32, 45
out, from 18°, #F2A03D), the core (27, #964614), "Marigold" below, on
#121418.

## Opening (ms from the first frame)

| Beat | When | What | Curve |
|---|---|---|---|
| Inhale | 0–180 | flower tucks: petals scale 0.86, −8 px radial, −10°; core 0.9; halo 0.18 → 0.10 | cubic-bezier(.45, 0, .55, 1) |
| Unfurl wave | from 180 | petal i springs back, delay 180 + 460·(i/n)^0.62 (the wave accelerates); a faint glow rides it (12 %, gone in 350 ms) | spring k 210, c 17 |
| Rings, core | 180 / 260 / 380 | outer ring turns 18°, inner −10°, core back to 1 | springs k 90 c 14 / k 140 c 16 / k 260 c 16 |
| Dim | 180–600 | unearned outer petals dim to 45 % | cubic-bezier(.4, 0, .2, 1) |
| Light | as earned | a petal lights when loading passes it, never before its own unfurl began (+80 ms), at most one every 45 ms; it pops 6 % | spring k 200, c 16 |
| Partial | waiting | the next petal fills with its share, ±6 % shimmer at 0.5 Hz | linear |
| Ground | 200–1100 | background warms, halo 0.10 + 0.30 × progress | cubic-bezier(.2, .7, .2, 1) |
| Waiting | after 1500 | breath ±1 % on 4 s; inner ring −1.5°/s | sine |
| Complete | ready | the rest light within 300 ms; the exit follows (never before 1150 ms) | — |

## Bloom-out (ms from the exit)

| Beat | When | What | Curve |
|---|---|---|---|
| Flash | 0–150 | every petal lit, halo up | cubic-bezier(.2, .8, .2, 1) |
| Release | 100–600 | petals drift +24 px and grow 10 % in an accelerating wave (100 + 200·(i/12)^0.62); flower to 1.12; blur to 14 px (soft discs); opacity to 0 | spring k 140, c 18; cubic-bezier(.4, 0, .2, 1) |
| Halo | 0–700 | flares to 0.5 at 200, then out | (.2, .8, .2, 1) then (.4, 0, .2, 1) |
| Guide | from 200 | 8 bands, top first, 50 ms apart, fade in over 300 ms | cubic-bezier(.2, .8, .2, 1) |

## Words

A line shows only once the wait passes 1.5 s, in small capitals (22 px,
0.16 em tracking, 55 % white), cross-fading between steps. Never a number,
a percentage or a provider.

| When | Line |
|---|---|
| Opening the saved list (a relaunch) | OPENING YOUR CHANNELS |
| Downloading | GETTING YOUR CHANNELS (after 20 s: STILL GETTING YOUR CHANNELS) |
| Sorting | SORTING YOUR LINEUP |
| Games | FINDING THIS WEEK'S GAMES |
| Finished, opening | ALMOST READY |

## Drawing

Rounded fills for the circles and one 128² soft disc texture (made in code)
for the glow, the halo and the blur, so it is the same on GLES2 and Metal and
cheap on an LG C1. The guide's bands come in under a veil of the warm ground;
there is no blur pass on the guide.
