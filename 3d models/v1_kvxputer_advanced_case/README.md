How it fits together

Base: your slip-in sleeve, unchanged, with the module bay added on the back. The NFC and RFID sides are 9.4 mm deep. The middle has a rounded bulge for the PaHub with 17 mm of inside depth (your 16.3 mm plus clearance). Two ears at the top carry the joint.
Joint: the middle hinge piece. It sits over the slide-in opening and has two small tabs that stop the Cardputer sliding out. It pivots on the base, and you can swing it up about 90° to take the Cardputer out. The inside is hollow so all 4 Grove cables run through it, and each opening is big enough for a plug to pass.
Lid: carries the 4 modules facing outward, on your peg positions. The lid pivots on the joint. The cables cross right at that pivot line, so they barely bend when it opens.
Two covers: each slides its top edge into a groove and is held by 2 M3 screws at the bottom. They stay on, but you can remove them for setup. The base cover includes the PaHub bulge.

Assembly order: slide in the Cardputer, attach the joint to the base ears, then attach the lid to the joint. Each pivot uses 2 M3 self-tapping screws about 8 mm long. Tightening the two lid screws adds friction so the lid holds its angle. The parts need 8 M3 screws in total: 4 for the hinges and 4 for the covers.

Things to check before printing:

IR placement: I read your IR LEDs as pointing straight out of the board, so the lid cover has a 19 × 9 mm window over the LED end of the IR module. I estimated where that end sits from the photos. Hold the IR board against the lid and confirm the window lines up.
Module ends: connectors face the keyboard edge on the base and the hinge edge on the lid. On the base, the gap below the connectors is the cable area, and cables run up between the modules to two openings into the joint. I worked out which end each module's connector is on from your peg positions, so test-fit one module first.
Cardputer cable: I don't know where the Cardputer ADV's Grove port is. The joint has a slot down to the Cardputer's top edge for the PaHub input cable. If the port is on a side instead, that cable needs another route.
Closing: nothing latches the lid shut. Only the hinge friction keeps it closed. I can add magnets or a snap if you want.
Size: the parts line up with your sleeve, but the hinge adds about 11.5 mm on top.
Printing: the STLs are in their closed, assembled positions, so lay them out in the slicer. Print the base upright like your sleeve, the lid with its Cardputer-facing side down, the joint flat-top down, and the covers flat. The STLs total about 220 MB, so they're zipped.

Every dimension is a named setting in build.py in the zip (clearances, pivot positions, bay depths, IR window, screw positions), so you can change one and rebuild.

Files are in cardputer_adv_hinged_case.zip:

base.stl
joint.stl
lid.stl
base_cover.stl
lid_cover.stl
build.py
sdf.py