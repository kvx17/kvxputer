# Cardputer ADV Hinged Module Case — v3

A 3D-printable protective clamshell for the **M5Stack Cardputer ADV** that also carries **7 M5Stack units** with their plastic shells removed. It opens like a laptop and has a fold-out kickstand. The styling is angular and cyberpunk.

The base is built around the user's original slip-in sleeve. The Cardputer still slides in from the top edge.

### What changed in v3

- **Full keyboard access.** The rails above and below the screen cover are removed, and so is the lip along the bottom row. The keyboard opening now runs from the floor up to just under the screen window, so the number row and the bottom row are fully reachable.
- **Floor.** The sleeve now has a floor flush with the module bay, so the whole base has one level bottom.
- **Push-out holes.** The floor has a strip of 7 triangle holes. Push the Cardputer up through them with a finger to slide it out.
- **Module pegs.** The NFC and RFID peg pairs moved 3.6 mm toward the middle, clear of the cover screw posts. The PaHub pegs are unchanged.

> **Status:** designed and checked in CAD. No part has been test-printed yet, so print a test fit first (see [Before you print](#before-you-print)).

---

## Contents

| File | What it is |
|---|---|
| `base.stl` | Slip-in sleeve (with floor and open keyboard) + bottom module bay + hinge ears + kickstand lugs |
| `base_cover.stl` | Back cover: PaHub bulge, cable trough, armor plate |
| `kickstand.stl` | Fold-out stand: pivot tube + 2 truss legs |
| `joint.stl` | Middle hinge piece: links base ↔ lid, carries the cables, locks the Cardputer in |
| `lid.stl` | Top half carrying the 4 lid modules |
| `lid_cover.stl` | Lid cover: armor plate, IR window |
| `build.py`, `sdf.py` | Parametric generator (Python 3 + numpy + scipy) |

All STLs are exported **in their assembled (closed) positions**. Lay each one flat in your slicer before printing (see [Printing](#printing)).

---

## Bill of materials

### Electronics

| Qty | Part | Where it goes |
|---|---|---|
| 1 | M5Stack Cardputer ADV | Slides into the base sleeve |
| 1 | Unit NFC (ST25R3916) | Base bay, left area |
| 1 | Unit PaHub v2.1 | Base bay, centre bulge |
| 1 | Unit RFID 2 | Base bay, right area |
| 1 | Unit RF433R (receiver) | Lid, position 1 |
| 1 | Unit GPS v1.1 | Lid, position 2 |
| 1 | Unit IR | Lid, position 3 (LED end behind the pill window) |
| 1 | Unit RF433T (transmitter) | Lid, position 4 |

Take each unit out of its plastic shell. The bare PCB drops onto the printed pegs through the half-round notches at its narrow "neck".

### Cables (original M5Stack Grove / HY2.0-4P)

| Qty | Length | Use |
|---|---|---|
| 4 | 20 cm | PaHub → the 4 lid modules. They run up through the bay, through the joint and across the hinge, and the hinge needs some slack. |
| 2 | 10 cm | PaHub → NFC, PaHub → RFID (inside the bottom trough) |
| 1 | 10 cm | Cardputer Grove port → PaHub input (through the side notch) |

Store any extra cable length in the bottom trough.

### Hardware

| Qty | Part | Use |
|---|---|---|
| 6 | M3 × 8 mm self-tapping screw, pan or button head (plastite / PT type) | 2 × joint→base (pivot A), 2 × lid→joint (pivot B), 2 × kickstand |
| 2 | M3 × 8 mm self-tapping screw | Lid cover |
| 2 | M3 × 10–12 mm self-tapping screw | Base cover (long posts in the trough corners) |

Standard M3 machine screws also work if you tap the 2.6 mm pilot holes first.

### Filament

| Colour | Parts | Approx. material |
|---|---|---|
| Black | base, lid, joint, bottom layers of both covers | ~70 g |
| Orange | kickstand, raised armor layers of both covers | ~15 g |
| Grey (optional) | small pill inserts in the bottom strip | < 2 g |

**Total:** about 84 cm³ of solid model, which is roughly 80–105 g of PLA depending on infill.

**Recommended material:** PETG or PLA+. PETG holds the screw threads and friction hinges better over time. Plastic covers have very little effect on RF, GPS or NFC range; avoid filaments with metal or carbon fill.

---

## Printing

**General settings:** 0.4 mm nozzle, 0.2 mm layers, 3 walls, 15–20 % infill (gyroid). Use 4 walls on the base, joint and kickstand, because the screws thread into them.

| Part | Orientation on the bed | Supports |
|---|---|---|
| `base` | Upright, the way your original sleeve was printed: slot opening up, ears on top | Only under the ear/lug overhangs, if your printer needs them |
| `lid` | The face that touches the Cardputer down on the bed; pegs point up | None, or touching-buildplate only, for the rounded hinge cap |
| `joint` | Flat top face down on the bed; the Cardputer tabs point up | None. The inner channel roof bridges about 33 mm. |
| `kickstand` | Legs flat on the bed (outer face down) | Only under the pivot tube |
| `base_cover` | Inner face down; armor plate and bulge face up | Under the bulge's inside roof |
| `lid_cover` | Inner face down; armor plate faces up | None |

### Two-colour covers (filament swap)

1. Slice each cover with its outer face up.
2. Find the layer where the raised armor plate starts. That is 1.6 mm above the bed for both covers.
3. Add a **filament change** (M600 / colour change) at that layer: black below, orange above.

The slashes and cut-outs in the armor then show the black underneath. To print in one colour, skip this step.

---

## Assembly

1. **Mount the bottom modules.** Press NFC, PaHub and RFID onto the base pegs, components facing outward and connectors toward the bottom (keyboard) edge.
2. **Wire the bottom.**
   - Connect NFC and RFID to PaHub channels through the trough.
   - Run the Cardputer cable from the Cardputer's Grove port, through the side notch on that side, to the PaHub input.
   - Plug the 4 long cables into PaHub. Route them up the gaps between the modules and out through the 2 windows in the top wall.
3. **Fit the base cover.**
   - Tilt it and slide the top tongue into the groove under the top wall.
   - Swing it down and fit the 2 long screws in the bottom corners.
4. **Fit the kickstand.** Place the tube between the lugs and screw it in from both ends. Tighten until the legs hold any angle.
5. **Slide in the Cardputer** from the top edge, keyboard facing the open front.
6. **Fit the joint.**
   - Feed the 4 long cables in through the windows in its underside, then out of its front.
   - Seat it on the top edge; its tabs drop into the slot.
   - Screw it to the base ears through pivot A.
7. **Mount the lid modules** on the lid pegs, components facing outward and connectors toward the hinge:
   - RF433R
   - GPS
   - IR (LED end toward the free edge)
   - RF433T
8. **Attach the lid.**
   - Pass the cables across the hinge gap and plug them into the lid modules.
   - Screw the lid to the joint arms through pivot B.
   - Tighten to set the lid friction.
9. **Fit the lid cover.** Slide the top tongue into its groove, then fit the 2 bottom screws.

**Removing the Cardputer later:** open the lid, remove the 2 pivot-A screws and lift the joint off. Then push the Cardputer up through the triangle holes in the floor. The joint also swings up about 90° on pivot A once those screws are loosened.

---

## Using it

- **Laptop mode:** open the lid to the angle you want (it opens up to about 140°). Fold out the kickstand: about 10–15° gives a comfortable typing tilt.
- **Closed:** the lid covers the screen and keyboard. All modules face outward for the best antenna range.
- **IR:** the lid cover has a pill-shaped window over the IR LED end. Aim the case's free edge or lid face at the target.
- **Friction:** if the lid or kickstand flops, tighten its two pivot screws a quarter turn.

---

## ⚠️ Electrical note — read this before wiring

**Unit PaHub v2.1 is an I²C multiplexer (TCA9548A).** It only switches I²C devices.

| Unit | Interface | Works through PaHub? |
|---|---|---|
| Unit NFC | I²C | ✅ Yes |
| Unit RFID 2 | I²C | ✅ Yes |
| Unit GPS v1.1 | UART | ❌ No |
| Unit IR | Digital GPIO (timing-critical) | ❌ No |
| Unit RF433T / RF433R | Digital GPIO (timing-critical) | ❌ No |

The Cardputer ADV has a single Grove port, so only one non-I²C unit can be connected at a time. You can unplug and replug through the hinge gap, or add a manual switch.

The case has room for all the cables either way. Verify your wiring plan electrically before closing it up.

---

## Before you print

Check these against your own hardware first. They were estimated from photos.

1. **IR window position.** Hold the bare IR board on the lid pegs and confirm its LED end sits behind the pill window.
2. **Grove port side.** The cable notch is on both ends. Make sure the notch on your Cardputer's Grove side lines up with its port.
3. **Test fit.** Print the `joint` first (~2 h). It checks the pivot screws, the cable windows and the Cardputer retaining tabs against your printed base.

---

## Customising (`build.py`)

Every dimension is a named parameter at the top of `build.py`. To rebuild:

```bash
pip install numpy scipy
RES=0.2 python3 build.py                 # all parts  (RES = mesh resolution in mm)
RES=0.2 python3 build.py lid kickstand   # selected parts
```

Set `TEMPLATE=/path/to/sleeve.stl` to point at the original sleeve STL. Output goes to `OUTDIR` (default `./out`).

| Parameter | Default | Controls |
|---|---|---|
| `X_DP` | 56.6 | Outer face of the PaHub bulge + cable trough (depth of the back) |
| `X_SH` | 47.1 | Outer face of the thinner NFC / RFID areas |
| `TROUGH_Z` | 22.0 | Height of the full-depth cable trough |
| `HUMP_Y` | (3.2, 35.8) | Width / position of the PaHub bulge |
| `ARMOR` | 1.2 | Raised armor plate thickness on the covers |
| `IR_PILL` | y 16.4–40.4, z 5.6–14.6 | IR window position / size |
| `SIDE_NOTCH` | x 38.2–44.6, z 40–52 | Cardputer cable entry notches |
| `LEGS`, `LEG_FOOT` | — | Kickstand leg width and length |
| `KB_OPEN` | y −22.0–58.3, top z 35.0 | Keyboard opening in the sleeve front (raise `ztop` to show more, lower it for more screen-frame support) |
| `FLOOR`, `FLOOR_TRI` | — | Floor thickness and the push-out triangle pattern |
| `PEG_SHIFT` | 3.6 | How far the outer (NFC / RFID) peg pairs move toward the middle |
| `CLR_M3`, `PILOT_M3` | 1.7 / 1.3 (radii) | Screw clearance and pilot hole sizes; adjust for your printer's hole accuracy |
| `B`, `A`, `K` | — | Pivot positions: lid, joint, kickstand |

---

## Specifications

| | |
|---|---|
| Closed size | ~89 × 70 × 61 mm (L × H × D, including armor and kickstand tube); flat bottom at z = 0 |
| Lid opening | 0–140° (friction hinge) |
| Kickstand | 0–70° (friction), ~10–15° recommended |
| Running clearances | 0.3 mm on pivots, 0.2 mm on covers |
| Lid gap when closed | ~3.9 mm between the lid and the screen frame, so the lid never presses on the keys |
| Mesh | 0.2 mm resolution; all parts watertight |
