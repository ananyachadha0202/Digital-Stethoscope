# Circuit Diagram & Wiring Reference

## Block Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        SIGNAL CHAIN                             │
│                                                                 │
│  [Heartbeat]                                                    │
│      │                                                          │
│  [P103 Sensor]  ──→  [LM358 Amplifier]  ──→  [ADC0 / PC0]       │
│   Electret mic      Non-inverting gain        ATmega328P        │
│   + comparator      ~10–100× adjustable       10-bit, 125 kHz   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Schematic Description

### 1. P103 Sound Sensor

The P103 module contains an electret microphone and an onboard LM393 comparator.
- **VCC** → 5V
- **GND** → GND
- **AOUT** (analog out) → LM358 amplifier input

> Use the **AOUT** pin, not DOUT, to pass the raw analog signal to the amplifier.

---

### 2. LM358 Amplifier Stage (Non-Inverting)

```
         ┌──────── Rf ────────┐
         │                    │
P103     │    ┌───────┐       │
AOUT ────┴───→│ IN+   │       │
              │ LM358 ├───────┴──→ To ADC0
         ┌───→│ IN−   │
         │    └───────┘
        Rg
         │
        GND

Gain = 1 + (Rf / Rg)
```

| Resistor | Suggested Value | Resulting Gain |
|----------|----------------|----------------|
| Rf | 100 kΩ | — |
| Rg | 10 kΩ  | ×11 |

Adjust Rf/Rg ratio to bring heartbeat peaks into the 600–900 ADC count range.
LM358 is powered from 5V/GND (single supply). Output swings from ~0V to ~3.5V.

---

### 3. LED Indicators

```
PD7 (D7) ──→ 220Ω ──→ [Green LED Anode] → [Cathode] → GND
PB0 (D8) ──→ 220Ω ──→ [Red   LED Anode] → [Cathode] → GND
```

At 5V with a 220Ω resistor and ~2V forward voltage:
```
I = (5V − 2V) / 220Ω ≈ 13.6 mA   (within ATmega328P 40 mA max per pin)
```

---

### 4. Full Connection Table

| From | To | Notes |
|------|----|-------|
| P103 VCC | Arduino 5V | — |
| P103 GND | Arduino GND | — |
| P103 AOUT | LM358 IN+ (pin 3) | Analog audio signal |
| LM358 IN− (pin 2) | Junction of Rf/Rg | Feedback network |
| LM358 OUT (pin 1) | Arduino A0 (PC0) | Amplified signal to ADC |
| LM358 VCC (pin 8) | Arduino 5V | Single-supply operation |
| LM358 GND (pin 4) | Arduino GND | — |
| Arduino D7 (PD7) | 220Ω → Green LED+ | Green LED anode |
| Arduino D8 (PB0) | 220Ω → Red LED+ | Red LED anode |
| Green/Red LED− | GND | LED cathodes |

---

## Power Budget

| Component | Current Draw |
|-----------|-------------|
| ATmega328P (active) | ~15 mA |
| P103 Sensor | ~5 mA |
| LM358 | ~1 mA |
| One LED (on) | ~14 mA |
| **Total** | **~35 mA** |

Safely powered from USB via Arduino's onboard regulator (500 mA available).

---

## Placement Tips

- Keep the P103 sensor away from the LM358 and Arduino to reduce electromagnetic interference pickup.
- Use short wires on the ADC0 line from LM358 output to minimize noise.
- Place a 100 nF decoupling capacitor between LM358 VCC and GND, close to the IC.
- Press the sensor firmly and steadily against the chest — movement artifact is the primary source of false readings.
