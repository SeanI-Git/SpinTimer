# 🧪 SpinTimer1

A self-contained, configurable countdown timer for laboratory centrifuge machines — built around an ESP32 microcontroller. When the centrifuge lid closes, a magnetic reed switch triggers the countdown. When time expires, an audible alarm sounds and an LED flashes until the lid is lifted.

No phone app, no cloud, no subscription. Just plug it in, configure it once over WiFi, and it works forever.

---

## Features

- ⏱ **Configurable timer** — 0 seconds to 61 minutes, set via a browser-based web interface
- 🔔 **20 selectable alarm tones** — 10 piercing alert tones and 10 melodic calm tones, synthesized audio through a mini amplifier and speaker
- 🎵 **In-browser tone preview** — audition any tone through your phone's speaker before saving, directly from the web UI
- 💡 **Blue LED indicator** — blips on timer start, flashes during alarm
- 🧲 **Magnetic reed switch trigger** — automatically starts when the centrifuge lid closes
- 📶 **WiFi configuration portal** — opens for 5 minutes on power-up, then shuts off completely
- 🔁 **3 lid-open behaviours** — Reset, Pause, or Pause-then-Reset if the lid opens mid-countdown
- 💾 **Settings are saved** — configuration persists through power cycles
- 🔒 **No persistent WiFi** — radio shuts down after configuration, ensuring reliable audio output

---

## Hardware

| Component | Part |
|---|---|
| Microcontroller | ESP32-WROOM-32 |
| Trigger | NO Magnetic Reed Switch |
| Amplifier | PAM8403 Mini Amplifier Board |
| Speaker | 28mm Micro Speaker |
| Indicator | Blue LED, 3.0V–3.2V |
| Resistor (LED) | 220Ω (LED current limiting) |
| Capacitor (DC block) | 10µF electrolytic, ≥10V (audio noise suppression) |
| Resistor (audio filter) | 10kΩ, 1/4W (audio noise suppression, optional) |
| Capacitor (audio filter) | 100nF ceramic, ≥5V (audio noise suppression, optional) |
| Enclosure | Custom 3D printed |
| Power | USB-C 5V |

---

## Wiring Summary

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
| GND | PAM8403 Signal Ground |
| 5V (VIN) | PAM8403 VCC |
| GND | PAM8403 Power GND |
| PAM8403 L OUT+ | Speaker (+) |
| PAM8403 L OUT− | Speaker (−) |

> **Audio filter note:** R1 and C1 form an optional RC low-pass filter that reduces high-frequency PWM switching noise. C2 is the primary fix — it blocks the DC bias that the ESP32's LEDC peripheral holds on GPIO25 at idle, which is the main cause of audible hiss or whine from the speaker when no alarm is playing. C2's positive (+) leg faces GPIO25; negative (−) leg faces PAM8403. R1 and C1 must be used together — C1 alone without R1 provides little benefit.

---

## Alarm Tones

The 20 available tones are divided into two categories, both colour-coded in the web UI:

**🔴 Alert — piercing and attention-grabbing (tones 0–9)**

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

**🔵 Calm — melodic and less intrusive (tones 10–19)**

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

---

## WiFi Configuration

On first power-up (and every subsequent power cycle), the ESP32 broadcasts a WiFi network for **5 minutes**:

- **SSID:** `SpinTimer1`
- **Password:** `WashoeZephyr`
- **Web UI:** `http://192.168.4.1`

Connect any phone or computer to that network and open the address above in a browser to configure the timer duration, alarm tone, and lid-open behaviour. Each tone has a ▶ preview button that plays the sound through your device's own speaker so you can audition it before saving.

> **To reconfigure:** simply unplug and replug the USB-C cable. The 5-minute window reopens on every boot.

---

## Software

- **Platform:** Arduino IDE
- **Board:** ESP32 Dev Module
- **Core:** ESP32 Arduino Core 3.x
- **No external libraries required**

See [`BUILD_GUIDE.md`](BUILD_GUIDE.md) for full setup and flashing instructions.

---

## License

MIT License — free to use, modify, and build upon.
