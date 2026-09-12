# Building the hardware

This is how the marquee is physically put together: the panels, the
controller, power, wiring, the frames that hold it all to the wall, and the
diffuser. The firmware side is in the [main README](../README.md). The
[parts list](Parts%20List.md) has links for everything mentioned here.

Most of this is also shown in
[part 2 of the video series](https://youtu.be/viwx1L9F-r0) (the wall
installation) and [part 3](https://youtu.be/8G8unBqflx8) (wiring up a single
panel on the bench).

You do not have to build the big one. A single panel on a desk stand, a 3 A
power supply, and a controller is a complete marquee, and it's the best way to
get familiar with the firmware before committing to a wall.

## Panels

The display is made of 8×32 flexible WS2812B matrix panels, the kind sold
everywhere for a few dollars each in quantity. My wall has twelve of them for a
total of 384×8 = 3072 pixels.

Each panel comes with wires pre-soldered and labeled on the back:

* **IN** at one end and **OUT** at the other, each a 3-pin JST connector with
  5 V, ground, and data. Panels daisy-chain by plugging OUT into the next
  panel's IN.
* A separate pair of **power** leads in the middle.

Power can be fed in through either the JST connectors or the middle leads,
whichever suits the layout. On the single-panel desk model I put a barrel jack
on the back and feed the middle leads from that.

The data goes in at one end and scrolling text moves away from it, so with IN
on the right the text scrolls right-to-left, which is what you want. That's
`kReverseDirection = true` in the firmware config; if your IN ends up on the
left, set it to `false` instead of flipping the panel.

## Controller

The firmware runs on an ESP32. The board I use is the
[QuinLED-Dig-Quad](https://quinled.info/quinled-dig-quad/), which is designed
for exactly this job:

* An ESP32 module.
* Four LED outputs (`LED1`–`LED4`) with proper 5 V level shifting and series
  resistors, on screw terminals.
* Rows of 5 V and ground screw terminals for the panels, fed through fuses.
* Protection so it can be powered from the 5 V supply and plugged into USB at
  the same time without back-feeding the USB port.

On the Dig-Quad, `LED1`, `LED2`, and `LED3` are GPIOs 16, 3, and 1, which is
what my wall's config uses for its three sections. The default config uses
just GPIO 16, so a single panel goes on `LED1`. (GPIOs 1 and 3 are also the
ESP32's serial port, so once you're using `LED2` or `LED3` there's no USB
serial console; the firmware has serial debug output compiled out by default
anyway.)

A plain ESP32 dev board works too, and is what I used for testing. Be aware
that driving WS2812B from a bare 3.3 V GPIO is out of spec. It mostly works,
but on the wall I saw a visible ripple at the boundary between sections until
I switched to a board with level shifters. If you go this route, put a level
shifter and a small series resistor (I used 68 Ω) on each data line.

### Sections and data pins

Each panel is 256 pixels. I tested a chain of four panels (1024 pixels) on one
data pin and got a good frame rate, so rather than push further I split the
wall into three sections of four panels, each on its own data pin. FastLED
drives the three outputs in parallel and the firmware treats the whole thing
as one 384-pixel-wide display. The firmware supports up to three sections, and
every section must have the same number of panels.

One thing I learned the hard way: while installing, I temporarily had four
panels on one pin and one panel on another, and the seam between them
glitched. Keeping the sections symmetrical (and keeping OTA off; see the
README) fixed it.

## Power

The panels are 5 V. Current depends entirely on brightness and content:

* One panel on the desk: a 3 A supply is plenty.
* A few panels: 5 A.
* The twelve-panel wall: an 18 A supply (a Mean Well LRS-100-5).

Twelve panels at full white would want far more than 18 A, but the marquee
never does that. It runs at a low brightness (the firmware defaults to 15 out
of 255, and it's still bright), most pixels are dark most of the time, and the
firmware tells FastLED to cap the total current at `kLedMaxAmps`, so it will
dim the output rather than exceed the limit. Set that value to match your
supply.

On the wall, power is injected at the start of each section, i.e. at panels
1, 5, and 9, rather than relying on the little JST wires to carry current
through the whole chain. I ran 18 AWG cable along the back and used WAGO
lever connectors to branch off to each injection point.

## Wiring it up

For a single panel on a Dig-Quad:

1. Take one of the JST pigtails that comes with the panel and crimp ferrules
   onto the three wires. The wires come tinned, but ferrules make a much
   better connection in a screw terminal.
2. Data (the middle wire, usually green) into the `LED1` terminal.
3. 5 V into the 5 V row, ground into the ground row. Double-check which is
   which.
4. Plug the pigtail into the panel's IN connector.
5. Power supply into the board's power input terminals.

That's the whole thing; it's ready to flash.

### Configuration button

The firmware supports an optional button that puts the marquee into
configuration mode (hold about a second while running) or wipes its settings
(hold while powering on). It's just a momentary switch between a GPIO and
ground; the pin uses the ESP32's internal pull-up, so no other components are
needed. The GPIO is `kResetPin` in the config, default 2.

On the Dig-Quad, GPIO 2 is broken out on the pin header along the side of the
ESP32 module, labeled `Q3`, three positions along from a ground pin. I crimped
Dupont connectors onto a small button in a 3D-printed box and plugged it
straight onto the header. It's a bit hard to see once the module is installed,
so check the pinout before plugging in.

## Frames and mounting

Twelve floppy panels don't stay flat or evenly spaced on their own, so each
one gets a rigid frame, and the frames interlock into one continuous strip.
The design files are in this directory:

| File | Purpose |
| --- | --- |
| `Laser Cut Frame Parts/Interlocking Frame Inside.svg` | Grid for a middle panel, interlocking on both sides. |
| `Laser Cut Frame Parts/Interlocking Frame Outside L.svg`, `... Outside R.svg` | Grids for the two end panels: interlock on the inside edge only. |
| `Laser Cut Frame Parts/Standalone Frame.svg` | Grid for a single panel that doesn't join anything. |
| `Laser Cut Frame Parts/Alignment Jig.svg` | Drilling template for positioning the brackets (see below). |
| `Laser Cut Frame Parts/Desk Stand.svg` | Parts for the single-panel desk stand. |
| `3D Printed Parts/Wall Bracket for Frame.stl` | The bracket that holds a frame to the wall. Four per panel. |
| `CAD Models/Frame with Brackets.f3d`, `Desk Stand.f3d` | Fusion 360 sources. |
| `3D Printed Parts/*.url`, `CAD Models/*.url` | Links to the DIN rail mounts on Printables. |

**The grid** is a laser-cut sheet of MDF with an opening for every LED. It
sits in front of the panel, and the panel is taped to the back of it to hold
it in position.

**The brackets** are 3D printed and do two jobs: they screw to the wall, and
they clamp the panel against the back of the grid. Four per frame, one near
each corner. They also hold the frame off the wall, which leaves a channel
behind the display for running the wires so nothing is visible from the front.

**The interlock** is the shaped vertical edge of each grid, which fits into
its neighbor so adjacent frames register against each other. That's what
keeps the spacing even across the whole wall, so the twelve panels act as one
seamless display.

**The jig** is a frame-shaped piece with the interlock on its edges and holes
where the bracket screws go. The procedure for each panel after the first:

1. Interlock the jig with the last frame you mounted, and make sure it's
   straight.
2. Mark or drill the screw holes through the jig, and screw the four brackets
   to the wall.
3. Take the jig off. The brackets are now exactly where the next frame needs
   them.
4. Hang the next frame on the brackets and screw it down.

Getting the *first* frame right is the important part: everything else
registers off it. Measure the whole run before you start. I had to take my
first one down and remount it lower because the line was going to run into
the ceiling by the far end.

I wired up each section as it went up and tested it before moving on, which
caught a bad connection early. Even so, expect this to take a while; twelve
panels took me a weekend.

## Diffuser

The bare LEDs are harsh, and a diffuser makes the text much easier to read.
Mine is nothing more than plain printer paper:

* Cut a sheet to the exact size of each panel. A paper cutter with a stop set
  to the width makes them all identical.
* Run strips of adhesive along the edges of the back with a ¼" tape
  applicator (the kind used for scrapbooking).
* Stick it to the front of the grid.

The only hard part is the edges between sheets: they need to butt up exactly,
because either a gap or an overlap is obvious once it's lit. It's fiddly
because the tape grabs as soon as it touches. The upside is that it's
completely removable, so a bad one can be peeled off and redone.

## Controller and power supply mounting

Both the power supply and the Dig-Quad sit on a short piece of DIN rail in the
corner behind the end of the display. The power supply mount is
[a design from Printables](https://www.printables.com/model/78569-din-mount-for-meanwell-lrs-100-5-power-supply);
I [remixed it](https://www.printables.com/model/215444-quinled-dig-quad-din-rail-mount)
with a plate for the Dig-Quad so the two sit side by side. The wiring runs from
there up into the channel behind the frames.

## Desk model

For a one-panel marquee to sit on a desk: a `Standalone Frame`, the
`Desk Stand` parts (laser cut or 3D print them), a barrel jack on the back for
power, and a 3 A supply. It's the same firmware with the default
configuration, and it's what I use for development so I don't have to touch
the one on the wall.
