# ULP-Based Pulse Counter

## Problem

The original `PulseCounter` implementation uses GPIO interrupts with light sleep. During light sleep,
edge detection is unavailable, so it switches to level-based GPIO wakeup. This means the main CPU
wakes on every single pulse transition — at 1000 Hz that is 2000 wakeups per second, each carrying
the full overhead of exiting and re-entering light sleep.

## Why Not PCNT

The hardware Pulse Counter (PCNT) peripheral requires the APB clock, which is gated during light
sleep. It simply stops counting. It would reduce interrupt overhead while the CPU is actively running,
but does not solve the sleep/wake problem at all.

## Solution: ULP-RISC-V Coprocessor

The ESP32-S3 includes a ULP-RISC-V coprocessor that runs independently of the main CPU, powered
entirely by the RTC domain. It keeps running during light sleep (and deep sleep). This makes it the
only hardware path capable of counting pulses without waking the main CPU.

The design:
- ULP firmware runs a tight polling loop, detecting falling edges on up to `MAX_CHANNELS` configured
  GPIO pins and incrementing a per-channel counter in RTC slow memory.
- The main CPU wakes only for WiFi DTIM beacons (every 20 beacons ≈ 2 s) and reads the accumulated
  counts from RTC memory at that point. No GPIO-triggered wakeups for pulse counting at all.

## ULP Clock

The ULP-RISC-V runs from the internal 8 MHz RC oscillator (ULP_RISC_V_CORE_CLK). At 8 MHz:

- Polling loop throughput: ~400 000 – 500 000 iterations/s per channel (depending on number of
  active channels), well above Nyquist for 1000 Hz signals.
- 1 ms debounce window = 8 000 cycles at nominal frequency.

The RTC slow clock (≈136 kHz) is not used as the ULP CPU clock; it is only used for RTC timers.
Starting with slow clock would drop throughput far below Nyquist for high pulse rates.

## Concurrency

The ULP only ever **increments** `ulp_pulse_count[i]`; it never resets it. The main CPU only
**reads** `ulp_pulse_count[i]` and tracks `lastSeen` locally. On each `reset()` call:

```
delta = ulp_pulse_count[i] - lastSeen
lastSeen = ulp_pulse_count[i]
return delta
```

This is lock-free by construction. The only possible race is reading a value one increment behind;
the missed pulse appears in the next read. For a flow meter this is entirely acceptable. Aligned
32-bit reads are atomic on the hardware, so torn reads cannot occur.

## Overflow

`ulp_pulse_count[i]` is a `uint32_t`. At 1000 Hz: 2³² / 1000 ≈ 49.7 days to overflow.
The delta subtraction uses unsigned wraparound arithmetic and is self-correcting even through overflow,
as long as the readout interval is much shorter than the overflow period — which it is (seconds vs days).

## Debounce

Debounce is implemented in the ULP using the RISC-V `mcycle` counter (ticks at the ULP CPU clock,
≈8 MHz). After counting a falling edge, subsequent edges are ignored until `debounce_cycles` cycles
have elapsed. The main CPU computes:

```
debounce_cycles = debounce_us * ULP_RISCV_CLOCK_HZ / 1_000_000
```

with `ULP_RISCV_CLOCK_HZ = 8 000 000`. This is approximate (the RC oscillator is not trimmed to
better than ~10%), but for a Hall-effect sensor — which produces clean, bounce-free transitions —
this is entirely sufficient.

## GPIO Constraints

Only RTC-capable GPIOs (GPIO 0–21 on ESP32-S3) can be used. The GPIO is configured as an RTC input
with pull-down by the main CPU in `PulseCounterManager::create()` before the ULP starts. The ULP
then reads it via `ulp_riscv_gpio_get_level()`.

## Startup Sequence

`PulseCounterManager::create()` registers channels (up to `MAX_CHANNELS = 4`) and configures their
RTC GPIOs, but does not start the ULP. `PulseCounterManager::start()` must be called explicitly
after all channels are registered. It zeroes the RTC-memory counters (to avoid carrying over stale
values from previous runs), writes the channel configuration, loads the ULP binary, and starts it.

The ULP then runs indefinitely. No timer-based periodic execution — just a continuous tight loop.

## Public API Changes

The external API of `PulseCounter` and `PulseCounterManager` is unchanged. Existing consumers
(`FlowMeter`, `ElectricFenceMonitor`) require no modification. The only new requirement is that
`PulseCounterManager::start()` is called from the device startup code after all peripherals are
initialized.
