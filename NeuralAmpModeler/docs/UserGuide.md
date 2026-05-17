# Neural Amp Modeler — User Guide

## Overview

**Neural Amp Modeler (NAM) fork** is a guitar/bass amp and effects modeler that runs
neural network captures of real hardware — amplifiers, pedals, and entire
signal chains — at low CPU cost with high accuracy.

This plugin fork adds a number of additional features, such as creating Chains, model and IR files drag and drop capabilities, Library Browser and others.

It is available as:

- **Standalone app** (`NeuralAmpModeler-app`) — for use without a DAW.
- **VST3 plugin** (`NeuralAmpModelerAVX.vst3`) — for use inside any VST3 host.

---

## Signal Chain
[Stereo In] → Sum to mono → [Noise Gate] → [NAM Model] → [Tone Stack EQ] → [IR Cab] → [DC Block] → Expand to stereo → [Stereo Out]

The plugin accepts **stereo input and produces stereo output**.  
Internally the NAM model, noise gate, tone stack, and IR all operate in **mono** — the stereo input channels are summed to mono before processing, and the mono result is copied to both output channels.  


---

## Main Controls

### Input / Output Level
| Control | Range | Default | Description |
|---------|-------|---------|-------------|
| **Input** | −20 dB … +20 dB | 0 dB | Gain applied before the model |
| **Output** | −40 dB … +40 dB | 0 dB | Gain applied after the entire chain |

Both knobs accept drag, scroll, and double-click to type a value.

### Tone Stack (EQ)
Three-band tonestack modeled after classic amp EQ sections.

| Knob | Range | Default |
|------|-------|---------|
| **Bass** | 0 – 10 | 5 |
| **Middle** | 0 – 10 | 5 |
| **Treble** | 0 – 10 | 5 |

The **Tone Stack toggle** (small button next to the EQ section) bypasses all
three bands at once when disabled.

### Noise Gate
- **Threshold** — gate opens above this level (−100 dB … 0 dB, default −80 dB).
- **Active toggle** — enable/disable the gate without losing your threshold setting.

The gate is a look-ahead gain-reduction gate. Set the threshold just below your
guitar's noise floor for silent pauses without affecting playing dynamics.

---

## Loading Models

### Single NAM Model (`.nam`)
Click **"Select model…"** (or drag-and-drop a `.nam` file onto the model area)
to load a neural amp capture.

The model name is shown in the browser label. Use the **◀ ▶ arrows** to
step through other `.nam` files in the same folder.

Click the **✕** button to unload the current model.

### Model Library Browser
The **Model Library Browser** inside the plugin is powered by metadata produced
by the separate **NAM Model Manager** desktop application.

#### How they connect

NAM Model Manager allows you to manage your local collection of `.nam` and `.pnam` files,
reads their metadata, and writes a single index file called **`data.json`** to
a fixed location on disk:

| OS | Location |
|----|----------|
| **Windows** | `%APPDATA%\nam-model-manager\data.json` |
| **macOS** | `~/Library/Application Support/nam-model-manager/data.json` |

When you click the **library icon** in the plugin (next to the File Browser control), NAM reads that file and
displays the full tree of models with all their metadata (gear make/model,
tone type, loudness, ESR, etc.).

#### Automatic refresh

The plugin checks the **last-modified timestamp** of `data.json` every time you
open the Library Browser. If NAM Model Manager has updated the file since the
last open (e.g. because you downloaded new models), the plugin reloads the
index automatically — no DAW restart needed.

#### If the Library Browser shows an error

The browser will show an error message if:

- NAM Model Manager has never been run (no `data.json` exists yet), or
- the file is present but could not be parsed.

**Fix:** Open NAM Model Manager, point it at your models folder, and let it
finish scanning. Once it writes `data.json` the plugin's Library Browser will
work on the next open.

#### Model files stay on your disk

NAM Model Manager only writes the metadata index. The actual `.nam` files
remain wherever you stored them. The plugin loads them directly from those
paths at the moment you select a model in the browser.

> **Tip:** If you move your `.nam` files to a different folder after scanning,
> re-run NAM Model Manager to regenerate `data.json`. Models that can no longer
> be found at their recorded path will be skipped silently when loading a
> `.pnam` chain (with a warning dialog listing the skipped files).

---

## PNAM — Model Chains

A `.pnam` file is a **chain of NAM models** mapped across the **Voice knob**
range. This lets you seamlessly sweep between multiple amp captures with a
single knob.

### Loading a Chain
Drag-and-drop a `.pnam` file onto the model area, or use the file browser and
select a `.pnam` file. The model browser label updates to show the chain name.

### Voice Knob
When a `.pnam` chain is active:
- A **Voice** knob appears on the UI.
- The knob range (0 – 10) is divided into **slots**, one per model in the chain.
- **Tick marks** on the knob face show the slot boundaries.
- A **counter** (e.g. *2 / 5*) inside the knob face shows the current slot.
- **Hovering** over the Voice knob temporarily shows the active slot's model
  name in the model browser label.

Each slot in a chain can carry optional **parameter overrides** (output level,
bass, mid, treble) that are applied automatically when that slot becomes active.

### PNAM Chain Editor
Click the **chain editor button** (three-line icon, right side of the model
browser row) to open the **PNAM Chain Editor** window. The button is only
enabled when a `.pnam` file is loaded.

In the editor you can:
- **New / Open / Save / Save As** `.pnam` files.
- Add, remove, and reorder **slots**.
- Set each slot's **Voice knob range** (min / max, 0 – 10).
- Assign a `.nam` file to each slot.
- Set per-slot **parameter overrides** (output level, tone bass/mid/treble).
- **Preview** a single slot or the whole chain in real time without saving.
- Adjust the editor **font size** with the +/− buttons.

---

## IR Cabinet Simulation

The **IR (Impulse Response)** section loads a `.wav` cabinet impulse response.

- Click **"Select IR…"** or drag-and-drop a `.wav` file onto the IR area.
- Use the **◀ ▶ arrows** to step through other `.wav` files in the same folder.
- The **IR toggle switch** bypasses the cabinet IR while keeping it loaded.
- Click **✕** to unload the IR.

> **Tip:** NAM models often already include cab coloration. Use the IR bypass
> toggle to compare with and without the additional IR.

---

## Slimmable Models

Some `.nam` models are **slimmable** — they support a continuous quality/CPU
trade-off. When a slimmable model is loaded, a **Slim icon** appears next to
the model browser and a **Slim knob overlay** becomes accessible.

- **Slim = 1.0** (rightmost) — full quality, full CPU.
- **Slim = 0.0** (leftmost) — minimum CPU, reduced quality.

Click the slim icon to open the overlay knob. Dismiss it by clicking anywhere
outside it.

---

## Input Calibration

The input calibration system lets you match the plugin's expected input level
to your audio interface's operating level, so NAM models behave as if they were
receiving the correct hardware signal level.

| Control | Description |
|---------|-------------|
| **Calibrate Input toggle** | Enable calibration mode |
| **Input Calibration Level** | Reference level in dBu (−60 … +60 dBu, default +12 dBu) |

Access these via the **Settings** panel (gear icon, top-right corner).

---

## Output Mode

Found in the **Settings** panel:

| Mode | Description |
|------|-------------|
| **Raw** | Model output passed through unchanged |
| **Normalized** | Output level is automatically normalized based on model loudness metadata |
| **Calibrated** | Output is adjusted to match a calibrated reference level |

---

## Level Meters

Two vertical peak/RMS meters are displayed on the left and right edges of the
plugin:

- **Left meter** — Input level (after Input gain, before the model).
- **Right meter** — Output level (after the full chain).

Meters show peak hold and clip indication (red above 0 dBFS).
You can also drag-and-drop model or IR files directly onto the meters.

---

## Settings Panel

Click the **gear icon** (top-right corner) to open the settings overlay:

- Output Mode selector (Raw / Normalized / Calibrated)
- Calibrate Input toggle and level
- Any per-session diagnostic info

---

## File Drag and Drop

You can drag `.nam`, `.pnam`, or `.wav` files from Windows Explorer directly
onto any part of the plugin UI:

- `.nam` → loads as the active model.
- `.pnam` → loads as a model chain.
- `.wav` → loads as the cabinet IR.

---

## Multiple Plugin Instances

Each plugin instance maintains its own independent state:
- Loaded model / chain
- All knob values
- IR file
- Library browser search query, selected tag, and tree expansion state

There is no shared session state between instances.

---

## Supported Formats

| Item | Extensions |
|------|-----------|
| NAM model | `.nam` |
| NAM chain | `.pnam` |
| Cabinet IR | `.wav` |

---

## System Requirements

- **OS:** Windows 10/11 (x64), macOS
- **Plugin format:** VST3, AU
- **CPU:** x64 with SSE2 minimum; AVX2 recommended for best performance
- **Sample rates:** 44.1 kHz, 48 kHz, 88.2 kHz, 96 kHz (model is resampled
  automatically if host rate differs from model's native rate)

---

## Tips

- **No model loaded?** Audio passes through the tone stack and IR only — useful
  for testing your cab IR.
- **Hum / noise between notes?** Enable the Noise Gate and lower the threshold
  until silent pauses are clean.
- **CPU too high?** If the model is slimmable, reduce the Slim knob. Otherwise
  try a lighter model (smaller size).
- **Level mismatch with other plugins?** Use Output Mode → Normalized, or trim
  with the Output knob.
- **Voice knob hover:** hovering over the Voice knob while a PNAM chain is
  loaded shows the active model name in the browser — useful when a chain has
  many slots.
