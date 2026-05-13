# 🔧 SpinTimer1 — Complete Build Guide

This guide walks you through building SpinTimer1 from scratch — from ordering parts to your first successful timed spin. No prior electronics or programming experience is assumed. Every step is explained.

---

## Table of Contents

1. [Parts & Tools](#1-parts--tools)
2. [Understanding the Circuit](#2-understanding-the-circuit)
3. [Wiring It Up](#3-wiring-it-up)
4. [Setting Up Arduino IDE](#4-setting-up-arduino-ide)
5. [Installing ESP32 Board Support](#5-installing-esp32-board-support)
6. [Loading and Flashing the Sketch](#6-loading-and-flashing-the-sketch)
7. [First Boot & Configuration](#7-first-boot--configuration)
8. [Using the Timer Day-to-Day](#8-using-the-timer-day-to-day)
9. [3D Printed Enclosure](#9-3d-printed-enclosure)
10. [Troubleshooting](#10-troubleshooting)

---

## 1. Parts & Tools

### Electronic Components

| Qty | Component | Notes |
|---|---|---|
| 1 | ESP32-WROOM-32 development board | Any standard ESP32 DevKit with USB-C or Micro-USB will work |
| 1 | NO Magnetic Reed Switch | "NO" means Normally Open — the circuit is open until a magnet gets close |
| 1 | PAM8403 mini amplifier board | A tiny pre-built audio amplifier module, widely available |
| 1 | 28mm micro speaker | Match impedance to your PAM8403 (typically 4Ω or 8Ω) |
| 1 | Blue LED | Forward voltage 3.0V–3.2V |
| 1 | 220Ω resistor | Protects the LED from excess current |
| 1 | 10µF electrolytic capacitor | ≥10V rating. DC-blocking capacitor on the audio signal path — primary fix for idle speaker hiss. **Observe polarity** (see wiring section). |
| 1 | 10kΩ resistor, 1/4W | Part of the optional RC low-pass audio filter — must be used together with the 100nF capacitor below |
| 1 | 100nF ceramic capacitor | ≥5V rating. Part of the optional RC low-pass audio filter — must be used together with the 10kΩ resistor above. No polarity. |
| — | Jumper wires or hookup wire | For connecting components together |
| — | Breadboard (optional) | Useful for prototyping before soldering |
| 1 | USB-C cable + 5V USB power adapter | Standard phone charger works fine |
| 1 | Small magnet | To be attached to the centrifuge lid |

> **On the audio filter components:** The 10µF electrolytic capacitor (C2) is the primary noise fix and should always be installed. The 10kΩ resistor (R1) and 100nF ceramic capacitor (C1) are an optional add-on — they reduce high-frequency PWM switching noise further, but are only needed if hiss persists after installing C2. R1 and C1 must always be used together; C1 alone without R1 does almost nothing.

### Tools

- Soldering iron + solder (if making a permanent build)
- Small piece of perfboard (for mounting the audio filter components neatly)
- Wire strippers
- Multimeter (recommended — useful for verifying capacitor polarity before soldering)
- Computer with a USB port (Windows, Mac, or Linux)

---

## 2. Understanding the Circuit

Before wiring anything, here's a plain-English explanation of what each component does and why it's wired the way it is.

### The ESP32

The ESP32 is the brain of the device. It's a small microcontroller — think of it as a tiny computer that runs one program (called a "sketch") on a loop. It has a built-in WiFi radio, which we use briefly for configuration, and a large number of "GPIO pins" (General Purpose Input/Output) that can be connected to external components to sense or control things.

### The Reed Switch

A reed switch is a tiny sealed glass tube containing two metal contacts. When a magnet is brought close, the contacts snap together and complete a circuit. We're using a "Normally Open" (NO) switch, meaning the circuit is open (disconnected) when no magnet is present, and closes (connects) when the magnet is near.

One leg of the reed switch connects to GPIO27 on the ESP32. The other leg connects to GND (ground). The ESP32 is configured to use its **internal pull-up resistor** on GPIO27, which means:
- **No magnet (lid open):** GPIO27 reads HIGH (3.3V)
- **Magnet present (lid closed):** GPIO27 reads LOW (0V, connected to GND)

This is how the ESP32 knows whether the centrifuge lid is open or closed.

### The LED

The blue LED is connected to GPIO32 through a 220Ω resistor. The resistor is **mandatory** — without it, too much current would flow through the LED and burn it out instantly. LEDs only allow current to flow in one direction, which is why polarity matters: the longer leg (Anode, +) goes toward the resistor/GPIO pin, and the shorter leg (Cathode, −) goes to GND.

### The PAM8403 Amplifier & Speaker

The ESP32 generates audio by rapidly switching GPIO25 on and off at specific frequencies — this is called PWM (Pulse Width Modulation). The PAM8403 amplifier takes that weak signal and boosts it enough to drive a speaker at audible volume. The speaker converts the electrical signal into sound waves.

> **Why GPIO25?** Audio on the ESP32 uses the LEDC (LED Control) peripheral to generate PWM signals. The LEDC peripheral and the WiFi radio cannot reliably operate simultaneously on this hardware, which is why WiFi is shut down before any audio plays.

### The Audio Filter (C2, R1, C1)

Even when no alarm is playing, the ESP32's LEDC peripheral holds GPIO25 at a non-zero DC voltage. The PAM8403 faithfully amplifies whatever is on its input — including that idle DC offset — which produces an audible hiss or whine from the speaker at rest. Three passive components inserted in-line between GPIO25 and the PAM8403 input eliminate this:

**C2 — 10µF electrolytic capacitor (primary fix, always install)**
Placed in series on the signal wire, C2 blocks the DC offset from reaching the PAM8403 entirely. AC audio signals pass through freely; DC does not. This is the main cause of idle hiss and C2 alone should resolve it. Polarity matters — see the wiring section.

**R1 + C1 — RC low-pass filter (optional, install if hiss persists)**
R1 (10kΩ, in series) and C1 (100nF, from the signal wire to GND) work as a pair to form a passive filter that attenuates high-frequency PWM switching noise before it reaches the amplifier. These two must always be installed together — C1 alone without R1 is nearly ineffective.

The complete signal path from ESP32 to amplifier is:

```
GPIO25 → R1 (10kΩ, in-line) → junction node → C2 (+) → C2 (−) → PAM8403 INL
                                     │
                                   C1 (100nF)
                                     │
                                    GND
```

---

## 3. Wiring It Up

Wire each connection as described below. If you're prototyping, use a breadboard and jumper wires. For a permanent install, solder the connections. The audio filter components (R1, C1, C2) are most neatly built on a small piece of perfboard and tucked inline on the signal wire between GPIO25 and the PAM8403.

### Reed Switch
```
ESP32 GPIO27  ───────────────  Reed Switch Leg 1
ESP32 GND     ───────────────  Reed Switch Leg 2
```
It doesn't matter which leg of the reed switch is which — they're interchangeable.

### Blue LED
```
ESP32 GPIO32  ───  220Ω Resistor  ───  LED Anode (longer leg, +)
ESP32 GND     ───────────────────────  LED Cathode (shorter leg, −)
```

### Audio Signal Path (GPIO25 → PAM8403)

This is the section that includes the noise-suppression components. Wire it in order, left to right:

```
ESP32 GPIO25
     │
    [R1 — 10kΩ resistor, in-line]        ← optional, but needed if adding C1
     │
     ├──── [C1 — 100nF ceramic] ──── GND  ← optional, only useful alongside R1
     │
    [C2 — 10µF electrolytic, (+) leg toward GPIO25 side, (−) leg toward PAM8403]
     │
PAM8403 INL
```

> ⚠️ **C2 polarity is critical.** The positive (+) leg of C2 must face the GPIO25/R1 side (higher DC potential). The negative (−) leg faces the PAM8403 INL pin. Installing an electrolytic capacitor backwards can cause it to fail or be damaged. If you have a multimeter, measure the DC voltage on each side of the C2 location before soldering to confirm which side is higher — it should be the GPIO25 side.

> If you are not installing R1 and C1, simply connect GPIO25 directly to the (+) leg of C2, and the (−) leg of C2 to PAM8403 INL.

### PAM8403 Power
```
ESP32 5V/VIN  ───  PAM8403 VCC  (Power)
ESP32 GND     ───  PAM8403 GND  (Signal ground — the one near INL)
ESP32 GND     ───  PAM8403 GND  (Power ground)
```

### Speaker
```
PAM8403 L OUT+  ───  Speaker (+) terminal
PAM8403 L OUT−  ───  Speaker (−) terminal
```

### Complete Pin Reference Table

| ESP32 Pin | Connects To |
|---|---|
| GPIO27 | Reed Switch Leg 1 |
| GND | Reed Switch Leg 2 |
| GPIO32 | 220Ω resistor → LED Anode (+) |
| GND | LED Cathode (−) |
| GPIO25 | R1 (10kΩ) in-line → junction node |
| Junction node | C1 (100nF ceramic) → GND |
| Junction node | C2 (+) leg (10µF electrolytic) |
| C2 (−) leg | PAM8403 INL (Left Audio In) |
| GND | PAM8403 Signal GND |
| 5V (VIN) | PAM8403 VCC |
| GND | PAM8403 Power GND |
| PAM8403 L OUT+ | Speaker (+) |
| PAM8403 L OUT− | Speaker (−) |

---

## 4. Setting Up Arduino IDE

The Arduino IDE is free software that lets you write code and send it to microcontrollers like the ESP32.

1. Go to [https://www.arduino.cc/en/software](https://www.arduino.cc/en/software)
2. Download and install the version for your operating system
3. Open Arduino IDE

---

## 5. Installing ESP32 Board Support

Arduino IDE doesn't know about ESP32 boards by default — you have to tell it where to find the support files.

### Step 1 — Add the ESP32 board manager URL

1. Open Arduino IDE
2. Go to **File → Preferences** (on Mac: **Arduino IDE → Preferences**)
3. Find the field labelled **"Additional boards manager URLs"**
4. Paste this URL into that field:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
5. Click **OK**

### Step 2 — Install the ESP32 boards package

1. Go to **Tools → Board → Boards Manager**
2. In the search box, type `esp32`
3. Find **"esp32 by Espressif Systems"** and click **Install**
4. Wait for the installation to complete (it downloads several hundred megabytes)

### Step 3 — Select your board

1. Go to **Tools → Board → esp32 → ESP32 Dev Module**
2. Go to **Tools → Port** and select the COM port that appeared when you plugged in your ESP32
   - On Windows this looks like `COM3` or `COM4`
   - On Mac/Linux it looks like `/dev/cu.usbserial-...`
   - If you don't see a port, you may need to install a USB driver — see [Troubleshooting](#10-troubleshooting)

---

## 6. Loading and Flashing the Sketch

"Flashing" means sending your compiled program to the ESP32's memory, where it will live permanently (until you flash something new).

1. Download `SpinTimer1.ino` from this repository
2. Open it in Arduino IDE (**File → Open**, then navigate to the file)
3. Arduino IDE requires the sketch file to be inside a folder with the same name. If it prompts you to create this folder, click **Yes**
4. Click the **→ Upload** button (the right-pointing arrow near the top left)
5. The IDE will compile the code (translate it into machine language) and then upload it
6. You'll see progress in the black output panel at the bottom
7. When you see `Hard resetting via RTS pin...` or `Done uploading`, the flash is complete

> **If you see errors during upload:** Make sure the correct Port is selected under Tools → Port, and that no other program is using that port.

---

## 7. First Boot & Configuration

### Connecting to the Configuration Portal

Every time the ESP32 is powered on, it creates a temporary WiFi network for **5 minutes** so you can configure it.

1. Power on the ESP32 via USB-C
2. On your phone or computer, open your WiFi settings
3. Connect to the network named **`SpinTimer1`** using the password **`WashoeZephyr`**
4. Open a web browser (Chrome, Safari, Firefox — any will do)
5. In the address bar, type exactly: **`http://192.168.4.1`** and press Enter

> **What is 192.168.4.1?**  
> When the ESP32 creates its own WiFi network (called an Access Point), it also acts as a tiny router. Just like your home router has an address (usually `192.168.1.1`) that you can visit to change its settings, the ESP32 uses `192.168.4.1` as its default address. You don't need an internet connection for this — your device is talking directly to the ESP32.

### Configuring the Timer

The web interface has three sections:

**Timer Duration**
Set the minutes and seconds for your centrifuge run. Both dropdowns go from 0 to 60, giving a range of 0:00 (instant alarm) up to 60:60 (61 minutes total).

**Alarm Tone**
Choose from 20 alarm tones divided into two colour-coded groups:

*🔴 Alert — piercing and attention-grabbing (tones 0–9)*

| # | Name | Character |
|---|---|---|
| 0 | Double Beep | Classic two-pulse beep |
| 1 | Triple Beep | Three rapid high-pitched pulses |
| 2 | Stutter | Very fast high-frequency burst |
| 3 | Warble | Two-tone alternating warble |
| 4 | Siren | Stepped sweep up and back down |
| 5 | Fast Pulse | Extremely rapid high pulses |
| 6 | Two-Tone | Rapid alternation between two high pitches |
| 7 | Chirp | Ascending chirp bursts in pairs |
| 8 | Dive Bomb | Sharp descend from very high to mid frequency |
| 9 | Industrial | Slow heavy pulses at high frequency |

*🔵 Calm — melodic and less intrusive (tones 10–19)*

| # | Name | Character |
|---|---|---|
| 10 | Rise | Gentle ascending arpeggio |
| 11 | Descend | Gentle descending scale |
| 12 | Ship Bell | Two soft bell strikes |
| 13 | Gentle Chime | Three-note musical chime (C–E–G) |
| 14 | Soft Arpeggio | Slow upward C major chord |
| 15 | Lullaby | Low, slow two-note pattern with long pauses |
| 16 | Trill | A/B alternating trill, flute-like |
| 17 | Pentatonic | Five-note pentatonic scale ascending |
| 18 | Eve Bell | Single soft tone with a slow decay pattern |
| 19 | Slow Waltz | Three gentle notes in waltz rhythm |

Each tone has a **▶ preview button** beneath it. Tapping it plays that tone through your phone's own speaker so you can hear exactly what it sounds like before committing. A **STOP PREVIEW** bar appears while a tone is playing so you can cut it off at any time.

**Lid-Open Behaviour**
This controls what happens if the centrifuge lid is opened *while the countdown is already running*:

| Option | What it does |
|---|---|
| **Reset** | Immediately cancels the countdown and arms the timer again |
| **Pause** | Freezes the countdown; closing the lid again resumes from where it left off |
| **Pause then Reset** | First lid opening pauses the timer. If the lid is then closed and opened a *second* time, the timer fully resets |

Click **SAVE & ARM TIMER** when done. Your settings are saved to the ESP32's internal memory and will be remembered even after unplugging.

### What happens after 5 minutes

If you don't connect and configure within 5 minutes, the ESP32 shuts down its WiFi automatically and arms itself using the last saved settings (factory default: 5 minutes, Tone 0, Reset behaviour). The LED will blink three times quickly to signal it is armed and ready.

**To reconfigure at any time:** unplug the USB-C cable, wait a moment, and plug it back in. The 5-minute configuration window opens again on every boot.

---

## 8. Using the Timer Day-to-Day

Once configured, operation is fully automatic:

1. **Power on the ESP32** — the configuration portal opens for 5 minutes (you can ignore this if settings are already correct)
2. **Load your centrifuge** and close the lid
3. **The magnet on the lid** triggers the reed switch — the blue LED blips on for half a second to confirm the timer has started, then goes dark
4. **The countdown runs silently** in the background
5. **When time expires** — the alarm tone plays on loop and the LED flashes rapidly
6. **Open the centrifuge lid** — the alarm stops immediately and the device returns to standby, ready for the next run

---

## 9. 3D Printed Enclosure

The enclosure is designed to mount on the side of the centrifuge body, with the reed switch positioned so that a small magnet affixed to the centrifuge lid passes directly over it when the lid is closed.

**Mounting tips:**
- Use double-sided foam tape or M3 screws to fasten the enclosure to the centrifuge body
- A small neodymium disc magnet (6mm–10mm diameter) works well on the lid
- Position the magnet so it aligns squarely over the reed switch when the lid is fully closed
- Test alignment before permanent mounting by watching for the LED blip when the lid closes

---

## 10. Troubleshooting

### The ESP32 doesn't appear as a COM port / serial port

Most ESP32 boards use a CH340 or CP2102 USB-to-serial chip. Windows and older Macs sometimes need a driver for these.

- **CH340 driver:** [https://www.wch-ic.com/downloads/CH341SER_EXE.html](https://www.wch-ic.com/downloads/CH341SER_EXE.html)
- **CP2102 driver:** [https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)

After installing, unplug and replug the ESP32.

### Upload fails with "Failed to connect to ESP32"

Hold down the **BOOT** button on the ESP32 board while clicking Upload in Arduino IDE. Release it once you see `Connecting...` in the output panel. Some boards require this to enter programming mode.

### I can't find the `SpinTimer1` WiFi network

- Make sure the ESP32 is powered on and the USB cable is data-capable (not charge-only)
- The network is only available for 5 minutes after power-on — unplug and replug to reopen the window
- Try moving closer to the ESP32

### The web page at 192.168.4.1 won't load

- Confirm your device is connected to `SpinTimer1` and not your regular home WiFi
- Make sure you typed `http://192.168.4.1` — some browsers auto-add `https://` which will not work here; you must use `http://`
- Try a different browser

### Speaker produces a hiss or whine when no alarm is playing

This is caused by the ESP32's LEDC peripheral holding a DC voltage on the audio output pin at idle. The fix is to install the audio filter components (C2, and optionally R1 + C1) described in the wiring section.

- **First step:** Install C2 (10µF electrolytic capacitor) in series on the signal wire between GPIO25 and PAM8403 INL. Double-check polarity — (+) toward GPIO25, (−) toward PAM8403. This alone should resolve or significantly reduce the hiss.
- **If hiss persists:** Add R1 (10kΩ resistor, in-line before C2) and C1 (100nF ceramic capacitor, from the R1/C2 junction to GND). These must be added together — C1 alone without R1 is not effective.

### No sound from the speaker

- Confirm the audio signal path is wired correctly: GPIO25 → (R1 if fitted) → (C1 to GND if fitted) → C2 (+) → C2 (−) → PAM8403 INL
- Check that the PAM8403 is powered from the ESP32's 5V and GND pins
- Check that the speaker is wired to L OUT+ and L OUT− on the PAM8403
- The alarm will only sound after the countdown completes — make sure the timer has actually expired
- WiFi must be off for audio to work. If the timer was triggered before the 5-minute AP window closed, something unexpected occurred — try reflashing the sketch

### The reed switch doesn't trigger / triggers randomly

- Check that GPIO27 connects to one leg of the switch, and GND to the other leg
- Make sure the magnet on the lid passes close enough to the switch (within ~5–10mm for most reed switches)
- If it triggers randomly without the magnet, check for a loose GND connection

### LED doesn't light up

- Verify polarity: the longer leg (Anode) must connect toward GPIO32 via the 220Ω resistor; the shorter leg (Cathode) goes to GND
- Check that the 220Ω resistor is in series (in-line) between GPIO32 and the LED, not bypassed

---

*Built with ❤️ for the lab bench. Contributions and improvements welcome.*
