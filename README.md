# LED Marquee

This is my LED marquee project. It is the coolest thing I've ever made.

[![Video thumbnail. An excited YouTuber pointing to a scolling message sign in his basement.](https://github.com/user-attachments/assets/00b8447b-7ae2-4367-a119-1b514e1f1549)](https://youtu.be/W0_3rzvq9Ks)

It's a 384×8 pixel scrolling sign made from twelve inexpensive 8×32 WS2812B
flexible LED matrix panels, driven by an ESP32 running the custom firmware in
this repository. It's been on my basement wall since 2022, showing a clock and
whatever text gets sent to it over MQTT.

The firmware is written in C++ on the Arduino framework with
[FastLED](https://fastled.io/) doing the heavy lifting of clocking pixels out
in parallel. It has a web interface, an MQTT API, Home Assistant integration,
over-the-air updates, and a captive-portal setup flow, so once it's on the wall
you never have to plug it into a computer again.

You don't need twelve panels. The same firmware runs a single panel sitting on
a desk, which is a good way to try it out.

## Video series

I covered the whole project in three videos:

1. [Overview and demo](https://youtu.be/W0_3rzvq9Ks): what it is, and a tour of
   all the features.
2. [Building and installing the hardware](https://youtu.be/viwx1L9F-r0): the
   frames, brackets, wiring, power, and how it went up on the wall.
3. [Connecting and configuring](https://youtu.be/8G8unBqflx8): a start-to-finish
   walkthrough of wiring up a single panel, building and flashing the firmware,
   and running the example clients. This one has chapter markers.

Everything in the videos is also written up here. For hardware, see
[hardware/README.md](hardware/README.md) and the
[parts list](hardware/Parts%20List.md).

## Features

* **Multiple display regions.** The marquee is treated as one wide canvas that
  can be divided up; currently that's a scrolling text area and an optional
  clock.
* **Web interface** for setting text, color, brightness, and speed.
* **MQTT API** for everything the web interface does and more, including a
  "ready" handshake so a client program can feed it a stream of messages.
* **Home Assistant integration.** The marquee announces itself via MQTT
  discovery and shows up as a light, so it can be turned off with the rest of
  the room.
* **Over-the-air firmware updates**, off by default and enabled on demand over
  MQTT.
* **Captive-portal setup.** On first boot it broadcasts its own WiFi network
  for entering your WiFi and MQTT settings. A button puts it back into
  configuration mode later, or wipes the settings entirely.
* **Diagnostics over MQTT.** Retained status, crash reports, and on-demand
  state snapshots, so problems can be investigated after the fact.
* **Self-recovery.** Task watchdog, reboot on sustained WiFi loss, and timeouts
  on every configuration mode, so a transient outage doesn't leave the sign
  stuck.

## Hardware summary

The minimum is one or more 8×32 WS2812B matrix panels, an ESP32, and a 5 V
power supply. I use a [QuinLED-Dig-Quad](https://quinled.info/quinled-dig-quad/)
as the controller because it has level-shifted outputs, screw terminals, and
fused power distribution all on one board; a bare ESP32 dev board works for
experimenting. An optional pushbutton between a GPIO and ground provides the
configuration/reset button.

Wiring, frames, power, and the full parts list are in
[hardware/README.md](hardware/README.md).

## Building the firmware

### Prerequisites

* [Visual Studio Code](https://code.visualstudio.com/) with the
  [PlatformIO IDE](https://platformio.org/install/ide?install=vscode)
  extension. (This also installs PlatformIO Core, so the `pio` command-line
  examples below work from the terminal PlatformIO adds to VS Code.)
* A USB cable to the ESP32 for the initial flash. After that you can use OTA.

### Get the code

```bash
git clone https://github.com/masto/LED-Marquee.git
```

Open the folder in VS Code. PlatformIO will detect the project and install the
platform and library dependencies listed in
[platformio.ini](platformio.ini).

### Run the setup script

There are two things the build needs that aren't checked in. Run this once from
the project directory:

```bash
./setup.sh
```

It copies [include/marquee_config.h.dist](include/marquee_config.h.dist) to
`include/marquee_config.h` (your local configuration, which is gitignored) and
downloads the [Huebee](https://huebee.buzz/) color picker assets into
`data/www/` for the web interface. It's safe to run again; it won't overwrite
anything that exists.

### Configure the build

All of the compile-time settings are in `include/marquee_config.h`, with
comments explaining each one. The defaults describe a single panel on GPIO 16
with the button on GPIO 2, which is right for a Dig-Quad with a panel on the
`LED1` terminal. The ones you're most likely to change:

| Setting | What it does |
| --- | --- |
| `kLedPins` | Data pin for each section. On a Dig-Quad, `LED1`–`LED3` are GPIOs 16, 3, and 1. |
| `kResetPin` | GPIO for the configuration/reset button (has an internal pull-up). |
| `kPanelsPerSection` | Panels daisy-chained on each data pin. Four is a good limit for frame rate. |
| `kMarqueeSections` | Number of data pins in use, up to 3. |
| `kReverseDirection` | `true` if the data-in end of the marquee is on the right (it usually is). |
| `kLedMaxAmps` | Current cap, enforced by FastLED. Match your power supply. |
| `kTimeZone` | POSIX TZ string for the clock. |
| `kClockWidth` | Pixel width of the clock region, or `0` for no clock. |
| `kSetupAp` | Name of the WiFi network broadcast during initial setup. |

The marquee is modeled as one or more *sections*, each a chain of *panels*,
each section fed from its own data pin. Sections sit side by side and act as a
single wide display in software, but driving them in parallel keeps the frame
rate up on long marquees. My wall is `kPanelsPerSection = 4`,
`kMarqueeSections = 3`, `kLedPins[] = {16, 3, 1}`.

### Build and upload

There are two artifacts: the firmware, and a filesystem image containing the
web interface. The filesystem only needs to be uploaded once (and again if you
change anything under `data/`).

From the PlatformIO sidebar (the alien icon), under **esp32dev**:

1. **Build** to confirm everything compiles.
2. **Build Filesystem Image**, then **Upload Filesystem Image** (with the ESP32
   connected over USB).
3. **Upload** to flash the firmware.

Or from the command line:

```bash
pio run
```

```bash
pio run --target uploadfs
```

```bash
pio run --target upload
```

There's no serial output by default: it's compiled out because it can cause
scrolling glitches, and on a Dig-Quad the `LED2`/`LED3` outputs are the UART
pins. If you need it while debugging, set `LM_SERIAL_DEBUG` to `1` in
[src/debug_serial.h](src/debug_serial.h), rebuild, and watch at 115200 baud:

```bash
pio device monitor
```

## First-time setup

After the firmware upload the marquee boots, briefly shows `START`, and then
scrolls `Connect to LEDSetupAP to configure.` (the network name is `kSetupAp`).

1. Connect a phone or laptop to that WiFi network. If your device supports
   captive-portal detection, the configuration page pops up on its own;
   otherwise open <http://192.168.4.1>.
2. Choose **Configure WiFi**, pick your network, and enter the password.
3. Fill in the rest of the settings on the same page:
   * **mDNS hostname**: e.g. `marquee`, so you can reach it at
     `http://marquee.local/`.
   * **MQTT host / port / user / password**: your broker. Leave the host blank
     to run without MQTT (the web interface still works, but most of the
     interesting features need MQTT).
   * **MQTT node name**: a unique name for this marquee. Defaults to
     `marquee`; if you have more than one, give each its own. This becomes
     part of every MQTT topic.
4. Save. The marquee reboots, joins your WiFi, and scrolls the startup message
   (`LED Marquee v1.1`). It's ready.

The setup network stays up for five minutes; if nobody configures it in that
time the marquee reboots and tries the saved WiFi again (or, if there is none,
comes back with the setup network). This is what keeps it from getting stuck
when the router is down at the moment it boots.

Setting up an MQTT broker is out of scope here, but
[Mosquitto](https://mosquitto.org/) is easy to get running, and
[MQTT Explorer](https://mqtt-explorer.com/) is a handy tool for watching the
topics and publishing to them by hand.

## Using it

### Web interface

Browse to `http://<hostname>.local/` (or the IP address). It's plain, but it
works:

* **Text**, with two buttons: **Set Next Message** replaces what's on screen
  right away, and **Queue Next Message** waits for the current message to
  finish scrolling first, for a clean transition.
* **Color** of the scrolling text.
* **Brightness**, 0–255. The default is 15, because these panels are *very*
  bright.
* **Speed**, which is really the delay in milliseconds between scroll steps
  (1–100, default 40). Lower is faster.

### MQTT API

All topics live under `marquee/<node name>` (the prefix is `kMqttPrefix`).
Payloads are JSON. These are the ones you publish to:

| Topic | Payload | Effect |
| --- | --- | --- |
| `.../text` | `{"text": "Hello"}` | Queue a message to show after the current one finishes. Add `"scroll": false` to show it immediately as static (non-scrolling) text. |
| `.../display` | `{"enabled": true, "speed": 40, "color": "ff8800"}` | Any subset: display on/off, scroll delay in ms, text color as hex RGB. |
| `.../set` | `{"state": "ON", "brightness": 15, "color": {"r": 255, "g": 0, "b": 0}}` | Home Assistant JSON light schema. Any subset. |
| `.../ota` | `{"enabled": true}` | Enable over-the-air updates until the next reboot. |
| `.../diag/get` | anything | Request a diagnostic snapshot on `.../diag`. |

Text can contain inline color changes using `{#rrggbb}`, e.g.
`"Roses are {#ff0000}red"`. Messages are truncated to `kMaxMessageLen`
(1024) characters.

These are the ones the marquee publishes:

| Topic | Payload | Meaning |
| --- | --- | --- |
| `.../ready` | `{"ready": true}` | The current message has finished and nothing is queued. The marquee waits one second (`kSmWaitTime`) for a `.../text` message, then repeats the previous one and publishes `{"ready": false}`. |
| `.../status` | JSON, retained | Boot count, reset reason, IP, RSSI, heap, and a `crash` field if the previous run crashed. Published on each MQTT connect. |
| `.../crashlog` | text, retained | Crash report from the previous run; cleared on a clean boot. |
| `.../diag` | JSON, retained | Full state snapshot: WiFi, config mode, what's on screen, memory. Published on connect, on state changes, before any planned restart, and on request. |
| `.../diag/restart`, `.../diag/anomaly` | JSON, retained | Copies of the last snapshot taken before a restart or when something unexpected was detected, kept so they survive the next boot. |

The `ready` handshake is the heart of it: a client subscribes to `.../ready`
and, each time it sees `true`, publishes the next thing to `.../text`. That's
how the example programs work, and it means the content lives in a script on
some other machine that you can change without reflashing anything. (Because a
`.../text` message is queued rather than shown immediately, you can also publish
the next message any time before the current one finishes.)

### Example clients

[mqtt_client_examples/](mqtt_client_examples/) has two small Python programs
that use the `ready` handshake:

* `clock.py` writes out the current time in words.
* `fortune.py` shows random lines from a fortune file, in a random color.

Set up a virtual environment and install the one dependency (paho-mqtt):

```bash
cd mqtt_client_examples && python3 -m venv .venv && source .venv/bin/activate && pip install -r requirements.txt
```

Then run one, giving it your node name and broker:

```bash
python clock.py --marquee_node marquee --mqtt_server mqtt.example.com --mqtt_user someone --mqtt_pass secret
```

`fortune.py` takes the same arguments. The `fortunes.txt` in the repository is
just a placeholder; download the real file it points at from
<https://silgro.com/fortunes.txt> over the top of it (the script strips the
two-character prefix that file puts on every line).

If you want to show something else, copy one of these and change what
`on_ready` publishes. My
[literature clock](https://github.com/masto/marquee-lit-clock) is a bigger
example of the same pattern.

### Home Assistant

When MQTT is configured, the marquee publishes a discovery message to
`homeassistant/light/<node name>/config`. If Home Assistant's MQTT integration
is connected to the same broker with discovery enabled, a light entity named
after the node appears automatically. On/off turns the whole display off and
on, brightness is the display brightness, and the color is the text color.
Because it's a light, it can be included in scenes and automations along with
the room's real lights.

### Configuration button and factory reset

If you wired a button to `kResetPin`:

* **Hold it for about a second while the marquee is running** to enter
  configuration mode. The display shows `CONFIG: http://<ip address>`; open
  that URL to change the WiFi or MQTT settings. Saving reboots the marquee. If
  nothing is saved within ten minutes it reboots on its own.
* **Hold it while powering on** to factory reset. The display counts down
  (`CLR?`, `CLR?3`, `CLR?2`, `CLR?1`); keep holding through `CLR!` and all
  settings are erased. It reboots into first-time setup. Let go during the
  countdown to cancel.

### Over-the-air updates

Once the marquee is on the network you can flash it without a cable.

1. Create a file named `marquee.ini` next to `platformio.ini` (PlatformIO
   merges it in via `extra_configs`, so you don't have to edit the main file):

   ```ini
   [env:esp32dev]
   upload_port = marquee.local
   upload_protocol = espota
   ```

   using whatever hostname you configured.
2. OTA is disabled by default, because polling for updates costs a little
   performance and it's not something you want open all the time. Enable it by
   publishing `{"enabled": true}` to `marquee/<node name>/ota`. It stays enabled
   until the next reboot, so you'll do this before each update.
3. Click **Upload** in PlatformIO (or `pio run --target upload`). The marquee
   shows a progress bar, reboots, and comes back up on the new firmware. The
   same works for **Upload Filesystem Image**.

### Diagnostics

If something goes wrong on the wall, the retained `status`, `crashlog`, and
`diag` topics usually explain it without needing a serial cable. A crash from
the previous boot is reported on the next boot; a state snapshot is published
right before every deliberate restart, along with a short reason
(`config_saved`, `wifi_lost`, `auto_cfg_timeout`, `ota_update`, and so on) that
shows up as `last_restart` after the reboot. Publishing anything to
`.../diag/get` gets you a fresh snapshot on demand.

## Project layout

| Path | Contents |
| --- | --- |
| `src/` | Firmware. `main.cpp` has the WiFi, web, MQTT, OTA, and main-loop logic; the rest is display layout, scrolling, the clock, config storage, and health monitoring. |
| `include/marquee_config.h.dist` | Template for the compile-time configuration. |
| `lib/Interpolate/` | The `{#rrggbb}` color-escape parser, with native unit tests. |
| `data/www/` | Web interface, built into the filesystem image. |
| `mqtt_client_examples/` | Python programs that feed content to the marquee. |
| `hardware/` | Laser-cut frame parts, 3D-printed bracket, CAD models, parts list, and the [hardware build guide](hardware/README.md). |
| `partition_custom.csv` | Flash layout: two app slots for OTA, plus separate filesystems for user settings and the web assets. |
| `setup.sh` | One-time setup for a fresh clone. |

## Running the tests

The pieces that don't depend on hardware have unit tests that run on your
computer:

```bash
pio test -e native
```

## License

Apache 2.0; see [LICENSE](LICENSE). Contributions are welcome; see
[docs/contributing.md](docs/contributing.md).
