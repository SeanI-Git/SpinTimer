# 🧪 SpinTimer1

A self-contained, configurable countdown timer for laboratory centrifuge machines — built around an ESP32 microcontroller. When the centrifuge lid closes, a magnetic reed switch triggers the countdown. When time expires, an audible alarm sounds and an LED flashes until the lid is lifted.

No phone app, no cloud, no subscription. Just plug it in, configure it once over WiFi, and it works forever.

---

## Features

- ⏱ **Configurable timer** — 0 seconds to 61 minutes, set via a browser-based web interface
- 🔔 **10 selectable alarm tones** — synthesized audio through a mini amplifier and speaker
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
| Resistor | 220Ω (for LED current limiting) |
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
| GPIO25 | PAM8403 INL (Left Audio In) |
| GND | PAM8403 Signal Ground |
| 5V (VIN) | PAM8403 VCC |
| GND | PAM8403 Power GND |
| PAM8403 L OUT+ | Speaker (+) |
| PAM8403 L OUT− | Speaker (−) |

---

## WiFi Configuration

On first power-up (and every subsequent power cycle), the ESP32 broadcasts a WiFi network for **5 minutes**:

- **SSID:** `SpinTimer1`
- **Password:** `WashoeZephyr`
- **Web UI:** `http://192.168.4.1`

Connect any phone or computer to that network and open the address above in a browser to configure the timer duration, alarm tone, and lid-open behaviour.

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
