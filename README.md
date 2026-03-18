# SubGate8

**SubGate8** is a VCV Rack module that provides an **8-step gate sequencer** with **4 subdivisions per step**, **swing**, and **gate length control**. It is designed to be a compact, performance-friendly sequencer for rhythmic gating and clock modulation.

---

## 🚀 Features

- **8-step sequencer** with independent on/off toggles for **4 subdivisions per step** (32 total subdivision triggers)
- **Gate length control**: adjust the output gate duration relative to the subdivision length
- **Swing control**: adds groove by delaying every other subdivision
- **Step count** knob (1–8) to quickly change sequence length
- **Clock input** for sync to external clocks
- **Reset input** to jump back to step 1
- **Run/Stop input** (gate mode) for transport control
- **Gate output** for generating gate/trigger signals
- **End-of-cycle (EOC) output** fired when the sequencer wraps back to step 1

---

## 🧩 Controls & I/O

### Controls
- **Step/Subdivision Buttons (8 × 4 grid)**
  - Toggle on/off for each subdivision gate within each step
- **Gate Length** (knob)
  - Controls pulse width of the gate output (as a percentage of subdivision duration)
- **Swing** (knob)
  - Applies swing timing to odd subdivisions (subdivisions 2 and 4)
- **Step Count** (knob)
  - Sets the number of active steps from 1 to 8

### Inputs
- **Clock** – Advances the sequencer by one step on each rising edge
- **Reset** – Resets the sequence back to step 1 when triggered
- **Run** – Gate input to start/stop the sequencer (high = run, low = stop)

### Outputs
- **Gate** – Gate signal based on enabled subdivisions and gate length
- **EOC** – End-of-cycle trigger when the sequencer wraps from last step to step 1

---

## 🛠️ Building

This repository uses the standard VCV Rack plugin build system.

1. **Clone Rack SDK** or ensure you have it available locally.
2. In the plugin directory, run:

```bash
make
```

If your Rack SDK is not located at the default relative path (`../..`), set `RACK_DIR`:

```bash
make RACK_DIR=/path/to/Rack-SDK
```

---

## 📦 Distribution

To create a distributable zip (for sharing or release):

```bash
make dist
```

The ZIP will include the compiled plugin binary, `plugin.json`, and the `res/` folder.

---

## 📄 License

This project is marked as **proprietary** in `plugin.json`. See the included `LICENSE` file (if present) for details.

---

## 📝 Notes

- The module saves the run/stop state between sessions.
- The sequencer estimates clock period dynamically for subdivision timing, which works well with steady clocks.
