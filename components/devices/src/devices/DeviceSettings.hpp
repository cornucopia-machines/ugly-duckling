#pragma once

#include <Configuration.hpp>

using namespace farmhub::kernel;

namespace farmhub::devices {

struct DeviceSettings : ConfigurationSection {
    ArrayProperty<JsonAsString> peripherals { this, "peripherals" };
    ArrayProperty<JsonAsString> functions { this, "functions" };

    Property<bool> sleepWhenIdle { this, "sleepWhenIdle", true };

    /**
     * @brief How often to publish telemetry.
     */
    Property<seconds> publishInterval { this, "publishInterval", 5min };
    Property<Level> publishLogs { this, "publishLogs",
#ifdef FARMHUB_DEBUG
        Level::Verbose
#else
        Level::Info
#endif
    };

    /**
     * @brief How long without successfully published telemetry before the watchdog times out and reboots the device.
     */
    Property<seconds> watchdogTimeout { this, "watchdogTimeout", 15min };
};

}    // namespace farmhub::devices
