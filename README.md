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
```

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

