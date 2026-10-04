# BadBuilder

An on-device, visual DuckyScript/BadUSB editor for Flipper Zero. Build and edit `.txt` BadUSB scripts directly on the
Flipper, no phone or PC needed.

## What's here

```
badbuilder/
├── application.fam     # app manifest (appid: badbuilder)
├── badbuilder\_app.c     # entire app, single file
├── badbuilder.png        # 10x10 fap launcher icon (must live at app root)
└── README.md
badbuilder.fap           # compiled binary, ready to copy to the Flipper's SD card
```

## How to build it yourself (Momentum FBT)

1. Clone the Momentum firmware and set up its build tooling (one-time):

```bash
   git clone --recursive https://github.com/Next-Flip/Momentum-Firmware.git
   cd Momentum-Firmware
   ./fbt
   ```

   The first `./fbt` run downloads the ARM toolchain — it's slow once,
fast after.

2. Drop this app into the firmware's external apps folder:

```bash
   cp -r /path/to/badbuilder applications\_user/badbuilder
   ```

   (`applications\_user/` is scanned automatically by fbt; the folder name
doesn't need to match the appid, but keeping it `badbuilder` is clearer.)

3. Build just this app:

```bash
   ./fbt fap\_badbuilder
   ```

   The compiled `.fap` lands at:

```
   build/f7-firmware-D/.extapps/badbuilder.fap
   ```

4. Copy it to the Flipper's SD card at `/ext/apps/Tools/badbuilder.fap`
(or use `./fbt launch APPSRC=applications\_user/badbuilder` to build,
push over USB, and launch it in one step with the Flipper connected).

## Using the app

* **D-pad Up/Down** in the left pane moves through the DuckyScript command
list. The tooltip at the bottom always shows what the highlighted command
does, scrolling horizontally if the explanation is too long to fit.
* **Right** moves focus to the code pane (in Split layout); **Left** moves
back to commands.
* **OK** on a command either inserts it immediately (ENTER, SPACE, arrow
keys, etc.) or opens the keyboard to ask for an argument (STRING, DELAY,
CTRL/ALT/GUI/SHIFT + key). The new line is inserted right after the
current selection and gets selected.
* **OK** on an existing argument-taking line in the code pane re-opens it
for editing, prefilled with its current argument.
* **Hold OK** while in the code pane saves the current file.
* **Hold Down** on a selected code line deletes it.
* **Hold Left** opens Layout Settings (Split / Commands only / Code only).
The choice is written to `/ext/badusb/.badbuilder\_layout` and reloaded
on next launch.
* **Hold Right** opens the File menu: New, Open (browses
`/ext/badusb/\*.txt` and any other files in that folder), Save, Save As.
* **Back** exits; if there are unsaved changes you'll get a confirmation
screen (OK = save \& exit, Back again = discard, anything else = cancel).

Scripts are **never executed** by this app — it only edits and saves
DuckyScript text. Running them is left to the Flipper's own BadUSB app.

