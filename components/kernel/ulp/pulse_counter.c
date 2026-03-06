/**
 * ULP-RISC-V pulse counter firmware.
 *
 * Runs continuously at the ULP CPU clock (~8 MHz) regardless of main CPU sleep state.
 * Counts falling edges on up to MAX_CHANNELS RTC-capable GPIO pins.
 *
 * Configuration is written to RTC slow memory by the main CPU before calling ulp_riscv_run().
 * Counts are read from RTC slow memory by the main CPU at any time (lock-free).
 *
 * See docs/ULP-PulseCounter.md for design rationale.
 */

#include <stdint.h>

#include "ulp_riscv_gpio.h"
#include "ulp_riscv_utils.h"

#define MAX_CHANNELS 4

// ---------------------------------------------------------------------------
// Shared with main CPU (written before ULP start, read at any time)
// ---------------------------------------------------------------------------

/** Number of active channels. Written by main CPU before ULP starts. */
volatile uint32_t ulp_channel_count = 0;

/** RTC GPIO number for each channel. Written by main CPU before ULP starts. */
volatile uint32_t ulp_gpio_num[MAX_CHANNELS];

/**
 * Debounce window in ULP CPU cycles (~8 MHz).
 * Compute as: debounce_us * 8  (8 cycles per microsecond at 8 MHz nominal).
 * Written by main CPU before ULP starts.
 */
volatile uint32_t ulp_debounce_cycles[MAX_CHANNELS];

/**
 * Monotonically increasing pulse count per channel.
 * Only ever incremented by ULP. Read by main CPU to compute deltas.
 * Zeroed by main CPU before ULP starts to ensure clean state after reboot.
 */
volatile uint32_t ulp_pulse_count[MAX_CHANNELS];

// ---------------------------------------------------------------------------
// Internal ULP state (not accessed by main CPU)
// ---------------------------------------------------------------------------

static uint32_t last_level[MAX_CHANNELS];
static uint32_t last_edge_cycle[MAX_CHANNELS];

// ---------------------------------------------------------------------------

static inline uint32_t get_ccount(void) {
    uint32_t ccount;
    __asm__ __volatile__("csrr %0, mcycle" : "=r"(ccount));
    return ccount;
}

int main(void) {
    uint32_t n = ulp_channel_count;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t gpio = ulp_gpio_num[i];
        // GPIO was initialised as RTC input with pull-down by the main CPU.
        // Re-assert direction here in case of any state change before ULP start.
        ulp_riscv_gpio_input_enable(gpio);
        last_level[i] = ulp_riscv_gpio_get_level(gpio);
        last_edge_cycle[i] = get_ccount();
    }

    while (1) {
        uint32_t now = get_ccount();

        for (uint32_t i = 0; i < n; i++) {
            uint32_t level = ulp_riscv_gpio_get_level(ulp_gpio_num[i]);

            if (level != last_level[i]) {
                last_level[i] = level;

                if (level == 0) {
                    // Falling edge detected. Apply debounce.
                    uint32_t elapsed = now - last_edge_cycle[i];
                    if (elapsed >= ulp_debounce_cycles[i]) {
                        ulp_pulse_count[i]++;
                        last_edge_cycle[i] = now;
                    }
                }
            }
        }
    }

    // Unreachable, but satisfies the compiler.
    return 0;
}
