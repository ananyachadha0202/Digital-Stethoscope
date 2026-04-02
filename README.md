# 🩺 Digital Stethoscope — Bare Metal AVR

> A real-time cardiac sound analyzer built on ATmega328P using a P103 sound sensor and LM358 op-amp amplifier — written entirely in bare-metal C with zero Arduino HAL dependencies.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-ATmega328P-green.svg)
![Language](https://img.shields.io/badge/language-C%20%28AVR%29-orange.svg)
![Status](https://img.shields.io/badge/status-active-brightgreen.svg)

---

## 📌 Overview

This project implements a digital stethoscope capable of detecting heartbeat sounds, computing real-time BPM (beats per minute), and providing instant visual feedback via LEDs — all at the register level on an ATmega328P microcontroller.

**No Arduino libraries. No HAL. Pure AVR-C.**

Every peripheral — ADC, Timer, UART, and GPIO — is configured directly through hardware registers, making this an excellent reference for embedded systems learners and professionals alike.

---

## ✨ Features

- 🎙️ **Sound acquisition** via P103 electret microphone sensor
- 🔊 **Signal amplification** via LM358 dual op-amp (non-inverting configuration)
- 💓 **Real-time BPM computation** using a 5-sample RR-interval rolling average
- 🟢 **Green LED** — Normal sinus rhythm (60–100 BPM)
- 🔴 **Red LED** — Bradycardia (<60 BPM) or Tachycardia (>100 BPM)
- 📡 **UART Serial output** at 9600 baud with BPM + status tags
- ⚡ **Interrupt-driven 1 ms timer** (no busy-wait delays anywhere)
- 💾 **~900 bytes Flash, ~100 bytes SRAM** — extremely lean footprint

---

## 🗂️ Repository Structure

```
digital-stethoscope/
├── src/
│   └── stethoscope_baremetal.c   # Main application source
├── docs/
│   ├── circuit_diagram.md        # Wiring & schematic description
│   └── register_reference.md     # Peripheral register walkthrough
├── LICENSE
└── README.md
```

---

## 🔌 Hardware

### Components

| Component | Description |
|-----------|-------------|
| ATmega328P | Microcontroller (Arduino Uno/Nano board) |
| P103 Sound Sensor | Electret microphone with onboard comparator |
| LM358 | Dual op-amp for audio signal amplification |
| Green LED | Normal BPM indicator |
| Red LED | Abnormal BPM indicator |
| 2× 220Ω Resistors | Current limiting for LEDs |

### Pin Mapping

| Signal | MCU Pin | Arduino Label |
|--------|---------|---------------|
| Amplified audio in | PC0 / ADC0 | A0 |
| Green LED | PD7 | D7 |
| Red LED | PB0 | D8 |
| UART TX (serial out) | PD1 | D1 |

### Wiring

```
[Chest/Wrist]
     │
[P103 Sensor OUT] ──→ [LM358 IN+] ──→ [LM358 OUT] ──→ [A0 / ADC0]
                                                              │
                                                        ATmega328P
                                                         PD7 ──→ 220Ω ──→ 🟢 GND
                                                         PB0 ──→ 220Ω ──→ 🔴 GND
```

---

## ⚙️ Peripheral Configuration

### Timer0 — 1 ms Tick
```
Mode     : CTC (Clear Timer on Compare)
Prescaler: /64
OCR0A    : 249
Frequency: 16,000,000 / 64 / 250 = 1000 Hz  →  1 ms per ISR
```

### ADC
```
Reference : AVcc (with cap on AREF)
Channel   : ADC0 (PC0)
Prescaler : /128  →  ADC clock = 125 kHz
```

### UART
```
Baud Rate : 9600
UBRR      : (16,000,000 / (16 × 9600)) − 1 = 103
Format    : 8N1, TX only
```

---

## 🛠️ Build & Flash

### Prerequisites

```bash
# Ubuntu / Debian
sudo apt install gcc-avr avr-libc avrdude

# macOS (Homebrew)
brew tap osx-cross/avr
brew install avr-gcc avrdude
```

### Compile

```bash
avr-gcc -mmcu=atmega328p -DF_CPU=16000000UL -O2 \
        -Wall -Wextra \
        -o stethoscope.elf src/stethoscope_baremetal.c

avr-objcopy -O ihex stethoscope.elf stethoscope.hex
```

### Flash

```bash
# Replace /dev/ttyUSB0 with your serial port (Windows: COM3, macOS: /dev/cu.usbserial-*)
avrdude -c arduino -p atmega328p -P /dev/ttyUSB0 -b115200 \
        -U flash:w:stethoscope.hex
```

### Monitor Serial Output

```bash
# Linux/macOS
screen /dev/ttyUSB0 9600

# Or with minicom
minicom -b 9600 -D /dev/ttyUSB0
```

Expected output:
```
=== Digital Stethoscope (Bare Metal) ===
Listening for heartbeat sounds...
BPM: 72.3  [NORMAL]
BPM: 74.1  [NORMAL]
BPM: 58.2  [LOW - Bradycardia range]
```

---

## 🎛️ Tuning Parameters

All tunable constants are defined at the top of `stethoscope_baremetal.c`:

| Constant | Default | Description |
|----------|---------|-------------|
| `THRESHOLD` | `600` | ADC count to register a beat peak (0–1023). Raise if false triggers; lower if beats are missed. |
| `DEBOUNCE_MS` | `300` | Minimum ms between accepted beats (~200 BPM max). |
| `SAMPLE_BEATS` | `5` | Number of RR intervals averaged. More = smoother, slower. |
| `BPM_LOW` | `60` | Lower bound of normal range. |
| `BPM_HIGH` | `100` | Upper bound of normal range. |

**Calibration tip:** Temporarily add `uart_putu32(adc_read(0))` inside the main loop to print raw ADC values. Find your sensor's noise floor and peak — set `THRESHOLD` between them.

---

## 📐 Algorithm

```
1. Sample ADC0 continuously (blocking read, ~104 µs per sample)
2. Detect rising edge above THRESHOLD (with DEBOUNCE_MS guard)
3. Record timestamp via millis() into circular buffer [size = SAMPLE_BEATS]
4. When buffer full:
       avg_RR = sum(RR intervals) / (SAMPLE_BEATS - 1)
       BPM    = 60000 / avg_RR
5. Drive LEDs and transmit BPM over UART
```

---

## 📊 Memory Footprint

| Section | Size |
|---------|------|
| `.text` (Flash) | ~900 bytes |
| `.data` + `.bss` (SRAM) | ~60 bytes |
| Stack (estimated) | ~40 bytes |

Well within ATmega328P limits (32 KB Flash / 2 KB SRAM).

---

## ⚠️ Disclaimer

This device is a **hobbyist educational project** and is **not a medical device**. It must not be used for clinical diagnosis or medical decision-making. Always consult a qualified healthcare professional for any health concerns.

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).

---

## 🙋 Author

Built by [ananyachadha0202](https://github.com/ananyachadha0202) as a hands-on embedded systems learning project exploring sensor integration, analog signal conditioning, and bare-metal AVR peripheral programming.
