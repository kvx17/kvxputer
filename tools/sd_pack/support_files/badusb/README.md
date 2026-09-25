# BadUSB payloads

Ducky Script collection for kvxputer **USB → BadUSB**. Copied onto the SD card at `/support_files/badusb/`.

> Educational / authorized testing only. Run payloads only on machines you own or have **explicit** permission to test.

## Maintainers / major contributors

Payloads in this tree come from public Hak5 and Flipper-community libraries. Authors are credited in each script (`REM Author:` / payload README). Special mentions:

* [Hak5](https://hak5.org/) (*[USB Rubber Ducky payloads](https://github.com/hak5/usbrubberducky-payloads)*, *[Bash Bunny payloads](https://github.com/hak5/bashbunny-payloads)*, *[O.MG payloads](https://github.com/hak5/omg-payloads)*)
* [atomiczsec](https://github.com/atomiczsec) (author of many Hak5 library payloads: Bookmark-Hog, History-Pig, Copy-And-Waste, …)
* [I-Am-Jakoby](https://github.com/I-Am-Jakoby) (*[hak5-submissions](https://github.com/I-Am-Jakoby/hak5-submissions)*)
* [FalsePhilosopher](https://github.com/FalsePhilosopher) (*[BadUSB-Playground](https://github.com/FalsePhilosopher/BadUSB-Playground)* — Flipper Zero community ducky collection and build resources)
* [MG](https://github.com/OMG-MG) / [O.MG](https://github.com/O-MG)
* [kvx17](https://github.com/kvx17) (`kvx17/` originals)
* …plus many others named in the individual payload files

For extra assets used by some duckies, or to write your own, see [FalsePhilosopher/BadUSB-Playground](https://github.com/FalsePhilosopher/BadUSB-Playground).

## Organization

| Folder | Source |
|--------|--------|
| `rubberducky/` | [hak5/usbrubberducky-payloads](https://github.com/hak5/usbrubberducky-payloads) |
| `bashbunny/` | [hak5/bashbunny-payloads](https://github.com/hak5/bashbunny-payloads) |
| `OMG/` | [hak5/omg-payloads](https://github.com/hak5/omg-payloads) and O.MG community ports |
| `flipperzero/` | Flipper Zero community ducky payloads (FalsePhilosopher / I-Am-Jakoby / others) |
| `kvx17/` | Original kvxputer scripts |

## Usage on kvxputer

1. Copy this `badusb/` folder onto the SD card as `/support_files/badusb/`.
2. **USB → BadUSB → Browse files** and pick a `.txt` payload.
3. Scripts you create in the app are saved separately at `/kvxputer/scripts/badUSB`.
