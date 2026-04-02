/*
 * ============================================================
 *  Digital Stethoscope — Bare Metal AVR C
 *  Target  : ATmega328P (Arduino Uno/Nano hardware, no HAL)
 *  Clock   : 16 MHz external crystal (fuse: CKDIV8 unprogrammed)
 *
 *  Hardware Map:
 *    P103 Sound Sensor (via LM358 amplifier) → PC0 / ADC0 (A0)
 *    Green LED (normal BPM 60-100)           → PD7
 *    Red   LED (abnormal BPM)                → PB0 (D8)
 *    UART TX  (Serial Monitor @ 9600)        → PD1
 *
 *  Compile:
 *    avr-gcc -mmcu=atmega328p -DF_CPU=16000000UL -O2 \
 *            -o stethoscope.elf stethoscope_baremetal.c
 *    avr-objcopy -O ihex stethoscope.elf stethoscope.hex
 *    avrdude -c arduino -p atmega328p -P /dev/ttyUSB0 -b115200 \
 *            -U flash:w:stethoscope.hex
 * ============================================================
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

// ── CPU & Derived Constants ───────────────────────────────────
#define F_CPU           16000000UL
#define BAUD            9600UL
#define UBRR_VAL        ((F_CPU / (16UL * BAUD)) - 1)   // = 103

// ── Pin Definitions (direct register names) ──────────────────
#define GREEN_DDR       DDRD
#define GREEN_PORT      PORTD
#define GREEN_PIN       PD7

#define RED_DDR         DDRB
#define RED_PORT        PORTB
#define RED_PIN         PB0

// ── Tuning Parameters ────────────────────────────────────────
#define THRESHOLD       600u     // ADC counts (0-1023); tune to sensor
#define DEBOUNCE_MS     300u     // Min ms between two valid beats
#define SAMPLE_BEATS    5u       // RR intervals averaged for BPM
#define BPM_LOW         60u
#define BPM_HIGH        100u

// ── Timer0 millisecond tick ───────────────────────────────────
// Timer0 CTC, prescaler /64 → tick = 64/16MHz = 4 µs
// OCR0A = 249  → ISR fires every 250 ticks × 4 µs = 1 ms exactly
volatile uint32_t ms_ticks = 0;   // free-running ms counter

ISR(TIMER0_COMPA_vect)
{
    ms_ticks++;
}

static inline uint32_t millis(void)
{
    uint32_t t;
    uint8_t sreg = SREG;
    cli();
    t = ms_ticks;
    SREG = sreg;          // Restore interrupt state
    return t;
}

// ── Runtime State ────────────────────────────────────────────
static uint32_t beat_times[SAMPLE_BEATS];   // Circular timestamp buffer
static uint8_t  beat_index   = 0;
static uint8_t  beats_filled = 0;
static uint32_t last_beat_ms = 0;
static uint8_t  above_thresh = 0;           // Edge-detection flag

// ── Peripheral Init Functions ─────────────────────────────────

static void timer0_init(void)
{
    // CTC mode (WGM01=1), prescaler /64 (CS01=1, CS00=1)
    TCCR0A = (1 << WGM01);
    TCCR0B = (1 << CS01) | (1 << CS00);
    OCR0A  = 249;                           // (16e6/64/1000) - 1
    TIMSK0 = (1 << OCIE0A);                 // Enable compare-match ISR
}

static void adc_init(void)
{
    // Reference: AVcc with external cap on AREF
    // Channel  : ADC0 (MUX3:0 = 0000)
    // Prescaler: /128  → ADC clock = 125 kHz (within 50-200 kHz range)
    ADMUX  = (1 << REFS0);
    ADCSRA = (1 << ADEN)                    // Enable ADC
           | (1 << ADPS2)                   // Prescaler /128
           | (1 << ADPS1)
           | (1 << ADPS0);
}

static void uart_init(void)
{
    UBRR0H = (uint8_t)(UBRR_VAL >> 8);
    UBRR0L = (uint8_t)(UBRR_VAL);
    UCSR0B = (1 << TXEN0);                  // TX only
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); // 8N1
}

static void gpio_init(void)
{
    GREEN_DDR |= (1 << GREEN_PIN);          // PD7 output
    RED_DDR   |= (1 << RED_PIN);            // PB0 output
    GREEN_PORT &= ~(1 << GREEN_PIN);        // LEDs off
    RED_PORT   &= ~(1 << RED_PIN);
}

// ── ADC Blocking Read ─────────────────────────────────────────
static uint16_t adc_read(uint8_t channel)
{
    ADMUX = (ADMUX & 0xF0) | (channel & 0x0F);  // Select channel
    ADCSRA |= (1 << ADSC);                       // Start conversion
    while (ADCSRA & (1 << ADSC));               // Wait until done
    return ADC;                                  // 10-bit result
}

// ── UART Helpers ─────────────────────────────────────────────
static void uart_putchar(char c)
{
    while (!(UCSR0A & (1 << UDRE0)));       // Wait for empty buffer
    UDR0 = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putchar(*s++);
}

// Prints an unsigned 32-bit integer
static void uart_putu32(uint32_t n)
{
    char buf[11];
    uint8_t i = 0;
    if (n == 0) { uart_putchar('0'); return; }
    while (n) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) uart_putchar(buf[i]);
}

// Prints float with one decimal place (no libm needed)
static void uart_putfloat1(float f)
{
    if (f < 0) { uart_putchar('-'); f = -f; }
    uint32_t whole = (uint32_t)f;
    uint8_t  frac  = (uint8_t)((f - whole) * 10.0f + 0.5f);
    if (frac >= 10) { whole++; frac = 0; }
    uart_putu32(whole);
    uart_putchar('.');
    uart_putchar('0' + frac);
}

// ── LED Control ───────────────────────────────────────────────
static void set_leds(float bpm)
{
    if (bpm >= BPM_LOW && bpm <= BPM_HIGH) {
        GREEN_PORT |=  (1 << GREEN_PIN);    // Green ON
        RED_PORT   &= ~(1 << RED_PIN);      // Red  OFF
    } else {
        GREEN_PORT &= ~(1 << GREEN_PIN);    // Green OFF
        RED_PORT   |=  (1 << RED_PIN);      // Red  ON
    }
}

// ── Beat Detection ────────────────────────────────────────────
static void detect_beat(void)
{
    uint16_t val = adc_read(0);             // Read ADC channel 0

    if (val > THRESHOLD) {
        if (!above_thresh) {                // Rising edge only
            above_thresh = 1;
            uint32_t now = millis();

            if ((now - last_beat_ms) > DEBOUNCE_MS) {
                beat_times[beat_index] = now;
                beat_index = (beat_index + 1) % SAMPLE_BEATS;
                if (beats_filled < SAMPLE_BEATS) beats_filled++;
                last_beat_ms = now;
            }
        }
    } else {
        above_thresh = 0;
    }
}

// ── BPM Calculation ───────────────────────────────────────────
static float compute_bpm(void)
{
    // Reconstruct buffer in chronological order
    uint32_t ordered[SAMPLE_BEATS];
    for (uint8_t i = 0; i < SAMPLE_BEATS; i++) {
        ordered[i] = beat_times[(beat_index + i) % SAMPLE_BEATS];
    }

    uint32_t total = 0;
    for (uint8_t i = 1; i < SAMPLE_BEATS; i++) {
        total += (ordered[i] - ordered[i - 1]);
    }

    float avg_ms = (float)total / (float)(SAMPLE_BEATS - 1);
    if (avg_ms <= 0.0f) return 0.0f;

    return 60000.0f / avg_ms;
}

// ── Report via UART ───────────────────────────────────────────
static void report_bpm(float bpm)
{
    uart_puts("BPM: ");
    uart_putfloat1(bpm);

    if (bpm >= BPM_LOW && bpm <= BPM_HIGH)
        uart_puts("  [NORMAL]\r\n");
    else if (bpm < BPM_LOW)
        uart_puts("  [LOW - Bradycardia range]\r\n");
    else
        uart_puts("  [HIGH - Tachycardia range]\r\n");
}

// ═════════════════════════════════════════════════════════════
int main(void)
{
    gpio_init();
    timer0_init();
    adc_init();
    uart_init();
    sei();                                   // Enable global interrupts

    uart_puts("=== Digital Stethoscope (Bare Metal) ===\r\n");
    uart_puts("Listening for heartbeat sounds...\r\n");

    for (;;) {
        detect_beat();

        if (beats_filled >= SAMPLE_BEATS) {
            float bpm = compute_bpm();
            set_leds(bpm);
            report_bpm(bpm);
        }
    }
}
