# MAC-Based Device Selection and Build Consolidation

## Goal

Replace per-generation compile-time device selection with runtime MAC-address-based detection,
consolidating the CI build matrix from one binary per hardware generation to one binary per
chip platform (ESP32-S3 and ESP32-C6).

## Background

Currently `UD_GEN` is set at build time (e.g. `MK6`), and `main.cpp` uses `#if defined(MK6)`
to select which `DeviceDefinition` subclass to instantiate. This produces five separate
binaries (MK5, MK6, MK7, MK8, MKX), four of which target the same chip (ESP32-S3).

MK6 already contains a two-level hierarchy: the single `UglyDucklingMk6` class detects its
hardware revision at runtime via `macAddressStartsWith()` and adjusts behavior accordingly
(Rev1/Rev2 use `IOC2` for motor nSleep; Rev3 uses `LOADEN`). The plan extends this pattern
to the top level, eliminating the compile-time generation split for same-platform devices.

## Technical Challenge: Pin Lifecycle

Each device definition file currently declares its pins as `static const` globals inside a
shared `namespace farmhub::devices::pins`. Including more than one of these files in the same
compilation unit causes two problems:

1. **Name collisions** — all five files use the same namespace and the same pin names
   (`SDA`, `SCL`, `STATUS`, …) mapped to different GPIOs.
2. **Registry pollution** — `InternalPin::registerPin` writes into global name and GPIO maps,
   so all included devices' pins are registered at startup regardless of which device is
   actually running.

The solution is to make each `DeviceDefinition` subclass **own its pins as instance members**,
constructing them only when the device object is created. Since only one device is ever
instantiated at runtime (the one selected by MAC detection or `UD_GEN`), only that device's
pins are ever registered. The `namespace pins {}` blocks are removed entirely.

### Knock-on: `createBatteryDriver` must become virtual

Currently `createBatteryDriver` is a `static` method called _before_ the device object is
instantiated in `Device.hpp`. Once pins are instance members, the battery pin is only
available on a live object. The cleanest fix is to make `createBatteryDriver` a `virtual`
instance method (with a default implementation returning `nullptr`) and adjust
`startDevice()` to create the device instance first, then call `deviceDefinition->createBatteryDriver(i2c)`.

This also removes the need for the double-template on `startDevice` — the `TDeviceSettings`
template parameter may be enough once battery creation is virtual.

## Plan

### Step 1 — Move Pins to Instance Members and Make `createBatteryDriver` Virtual

For every `DeviceDefinition` subclass:

- Remove the `namespace pins { ... }` block.
- Declare each pin as a `const` member variable of the class, initialized via
  `InternalPin::registerPin(...)` in the member initializer list.
- Change `createBatteryDriver` from `static` to `virtual` in `DeviceDefinition`,
  with a default implementation returning `nullptr`.
- Update `startDevice()` / `initBattery()` in `Device.hpp` to instantiate the device
  object first, then call `deviceDefinition->createBatteryDriver(i2c)` as a virtual call.

The `statusPin` and `bootPin` passed to the `DeviceDefinition` base constructor must come
from the derived class's already-initialized members. In C++, base-class sub-objects are
initialized before member variables, so a helper base struct (initialized before
`DeviceDefinition<TSettings>` in the inheritance list) or constructor-local values are
needed. The simplest approach: call `InternalPin::registerPin` directly in the
`DeviceDefinition` constructor arguments, storing the result in both the base and in a
named member if needed elsewhere.

### Step 2 — Flatten MK6 Revision Hierarchy

Replace the single `UglyDucklingMk6` class (which contains internal revision detection) with
three explicit classes sharing a common base. Pins move from the namespace block to members
of `UglyDucklingMk6Base`:

```text
UglyDucklingMk6Base   (abstract — all MK6 pins as members, battery driver, shared peripheral setup)
├── UglyDucklingMk6Rev1   (overrides motorNSleepPin() → IOC2; MAC prefix 0x34:0x85:0x18)
├── UglyDucklingMk6Rev2   (overrides motorNSleepPin() → IOC2; MAC prefix 0xec:0xda:0x3b:0x5b)
└── UglyDucklingMk6Rev3   (overrides motorNSleepPin() → LOADEN; all other known MK6 MACs)
```

`Mk6Settings::motorNSleepPin` is removed; the correct pin is returned by a pure-virtual
`motorNSleepPin()` method that each subclass overrides.

### Step 3 — Flatten MK8 Revision Hierarchy

Similarly, replace `UglyDucklingMk8` with two classes. Pins move to members of
`UglyDucklingMk8Base`:

```text
UglyDucklingMk8Base   (abstract — all MK8 pins as members, battery driver, shared peripheral setup)
├── UglyDucklingMk8Rev1   (INA219 omitted; MAC prefix 0x98:0xa3:0x16:0x1a)
└── UglyDucklingMk8Rev2   (INA219 included)
```

`Mk8Settings::disableIna219` is removed.

### Step 4 — Replace `macAddressStartsWith` with `macAddressMatchesAny`

Replace the existing `macAddressStartsWith` in `MacAddress.hpp` with a variadic
`macAddressMatchesAny` that accepts any number of prefixes of any length:

```cpp
template <typename... Prefixes>
[[maybe_unused]]
static bool macAddressMatchesAny(const Prefixes&... prefixes) {
    return (... || std::equal(prefixes.begin(), prefixes.end(), getRawMacAddress().begin()));
}
```

Calling it with a single prefix is identical to the old `macAddressStartsWith`, so it is
a strict superset with no reason to keep both. All existing call sites (currently in the
MK6 and MK8 device files) are updated as part of the surrounding refactor steps.

Multiple prefixes of varying lengths can be passed in a single call:

```cpp
if (macAddressMatchesAny(
        std::array<uint8_t, 2>{0x11, 0x22},
        std::array<uint8_t, 4>{0xAA, 0xBB, 0xCC, 0xDD}))
```

### Step 5 — Introduce MAC-Based Top-Level Dispatch

Add a per-platform `dispatchToDevice()` function in `main/main.cpp` that selects the
appropriate `DeviceDefinition` at runtime. Both platforms follow the same structure; the
C6 binary currently has only one variant but is wired up identically for consistency and
to accommodate future additions.

`startDevice` is a template and must be called with concrete type parameters, so the
dispatch is a chain of `if` branches, each calling a different template instantiation.
All variants for that platform are compiled into the binary.

The MAC ranges for MK5, MK6 Rev3, and MK7 are not yet recorded in the codebase.
**Dummy placeholder ranges are used initially and must be replaced with actual ranges
from production records before the consolidated binary ships.**

```cpp
// Pseudocode — dummy ranges marked TODO

#ifdef CONFIG_IDF_TARGET_ESP32S3
static void dispatchToDevice() {
    // MK6 Rev1
    if (macAddressMatchesAny(std::array<uint8_t, 3>{0x34, 0x85, 0x18}))
        return startDevice<Mk6Settings, UglyDucklingMk6Rev1>();
    // MK6 Rev2
    if (macAddressMatchesAny(std::array<uint8_t, 4>{0xec, 0xda, 0x3b, 0x5b}))
        return startDevice<Mk6Settings, UglyDucklingMk6Rev2>();
    // MK6 Rev3 — TODO: replace dummy ranges
    if (macAddressMatchesAny(
            std::array<uint8_t, 2>{0xAA, 0x01},
            std::array<uint8_t, 3>{0xAA, 0x02, 0x03}))
        return startDevice<Mk6Settings, UglyDucklingMk6Rev3>();
    // MK5 — TODO: replace dummy range
    if (macAddressMatchesAny(std::array<uint8_t, 2>{0xAA, 0x00}))
        return startDevice<Mk5Settings, UglyDucklingMk5>();
    // MK7 — TODO: replace dummy range
    if (macAddressMatchesAny(std::array<uint8_t, 2>{0xAA, 0x04}))
        return startDevice<Mk7Settings, UglyDucklingMk7>();
    // MK8 Rev1
    if (macAddressMatchesAny(std::array<uint8_t, 4>{0x98, 0xa3, 0x16, 0x1a}))
        return startDevice<Mk8Settings, UglyDucklingMk8Rev1>();
    // MK8 Rev2 — TODO: replace dummy range
    if (macAddressMatchesAny(std::array<uint8_t, 2>{0xAA, 0x05}))
        return startDevice<Mk8Settings, UglyDucklingMk8Rev2>();

    ESP_LOGE("device", "Unrecognized MAC address %s", getMacAddress().c_str());
    abort();
}
#endif

#ifdef CONFIG_IDF_TARGET_ESP32C6
static void dispatchToDevice() {
    // MKX — TODO: add MAC range when known; currently the only C6 variant
    if (macAddressMatchesAny(std::array<uint8_t, 2>{0xAA, 0x10}))
        return startDevice<MkXSettings, UglyDucklingMkX>();

    ESP_LOGE("device", "Unrecognized MAC address %s", getMacAddress().c_str());
    abort();
}
#endif
```

The function is called from `app_main()` when no `UD_GEN` override is defined.

### Step 6 — Preserve UD_GEN Override

Keep `UD_GEN` as a compile-time escape hatch for cases where MAC-based detection is
inappropriate (Wokwi simulation, factory flashing a specific generation):

```cpp
// main.cpp
extern "C" void app_main() {
#if defined(MK5)
    startDevice<Mk5Settings, UglyDucklingMk5>();
#elif defined(MK6)
    // Default to Rev3 (latest) when generation is pinned without revision
    startDevice<Mk6Settings, UglyDucklingMk6Rev3>();
#elif defined(MK6_REV1)
    startDevice<Mk6Settings, UglyDucklingMk6Rev1>();
#elif defined(MK6_REV2)
    startDevice<Mk6Settings, UglyDucklingMk6Rev2>();
#elif defined(MK7)
    startDevice<Mk7Settings, UglyDucklingMk7>();
#elif defined(MK8)
    startDevice<Mk8Settings, UglyDucklingMk8Rev2>();
#elif defined(MK8_REV1)
    startDevice<Mk8Settings, UglyDucklingMk8Rev1>();
#elif defined(MKX)
    startDevice<MkXSettings, UglyDucklingMkX>();
#else
    dispatchToDevice();   // runtime MAC detection
#endif
}
```

The platform split (which device headers to include) is handled via
`CONFIG_IDF_TARGET_ESP32S3` / `CONFIG_IDF_TARGET_ESP32C6`, so each binary only pulls in
the headers relevant to its chip.

The CI build does not set `UD_GEN` for either platform — both go through `dispatchToDevice()`.
`UD_GEN` is only set when needed for local development or Wokwi simulation. The Wokwi e2e
test already passes `-DUD_GEN=MK6`; this continues to work unchanged.

### Step 7 — Consolidate CI Build Matrix

Replace the per-generation matrix entries with per-platform entries:

**Before (6 entries):**

```text
mk5-release   (esp32s3, UD_GEN=MK5)
mk6-release   (esp32s3, UD_GEN=MK6)
mk7-release   (esp32s3, UD_GEN=MK7)
mk8-release   (esp32s3, UD_GEN=MK8)
mk6-debug     (esp32s3, UD_GEN=MK6, UD_DEBUG=1)
mkX-release   (esp32c6, UD_GEN=MKX)
```

**After (4 entries):**

```text
esp32s3-release   (esp32s3, no UD_GEN — runtime detection)
esp32s3-debug     (esp32s3, no UD_GEN, UD_DEBUG=1)
esp32c6-release   (esp32c6, no UD_GEN — runtime detection)
esp32c6-debug     (esp32c6, no UD_GEN, UD_DEBUG=1)
```

Artifact filenames change from e.g. `ugly-duckling-mk6-release.bin` to
`ugly-duckling-esp32s3-release.bin`.

The `DeviceCommon.cmake` `UD_GEN` validation is relaxed: when `UD_GEN` is unset,
the default target (`esp32s3`) is used without a fatal error.

#### sdkconfig consolidation

All S3 generations (`sdkconfig.mk5.defaults` through `sdkconfig.mk8.defaults`) are
identical — all set `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`. MKX sets 8MB. Replace the
five per-generation files with two platform files:

- `sdkconfig.esp32s3.defaults` — `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`
- `sdkconfig.esp32c6.defaults` — `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`

`DeviceCommon.cmake` is updated to load `sdkconfig.{platform}.defaults` instead of
`sdkconfig.{ud_gen_lower}.defaults`.

### Step 8 — Update Tests

- **Embedded tests**: Remove `UD_GEN` from the embedded test build if it was generation-specific.
- **E2e tests**: The Wokwi e2e test already pins `UD_GEN=MK6`; no change needed.
- **Unit tests**: No chip dependency; no change needed.

## Open Questions

The following information is needed before Step 5 can be fully implemented:

1. **MAC address ranges**: What are the full MAC ranges (prefixes or ranges) that identify
   MK5, MK7, and the MK6 Rev3 total pool? The existing code has prefixes for MK6 Rev1/Rev2
   and MK8 Rev1, but the generation-level identifiers are missing (they were never needed
   before because generations were separated at build time).

2. **Unrecognized MAC policy**: Should an unrecognized MAC `abort()` (fail-fast, requiring a
   known device to ever boot), or should it log a warning and fall back to a safe default?
   The plan above assumes fail-fast.

## Execution Order

1. Step 1 (pins to instance members + virtual battery driver) — foundational; the device files
   can still be included one at a time with `UD_GEN` while this is being done
2. Steps 2–3 (flatten hierarchies) — behaviour-preserving; verify each MK revision separately
3. Step 4 (`macAddressMatchesAny`) — small, self-contained addition to `MacAddress.hpp`
4. Step 5 (MAC dispatch) — requires MAC range data from open question #1
5. Step 6 (UD_GEN override + multi-device includes in `main.cpp`) — straightforward once step 5 is done
6. Steps 7–8 (build/CI consolidation) — last, once runtime selection is proven correct
