#pragma once

#include <chrono>
#include <memory>
#include <utility>

#include <Configuration.hpp>
#include <Log.hpp>
#include <Named.hpp>

#include <functions/Function.hpp>
#include <peripherals/api/IValve.hpp>
#include <peripherals/api/TargetState.hpp>

using namespace farmhub::peripherals::api;

namespace farmhub::functions::thermostat {

LOGGING_TAG(THERMOSTAT, "thermostat")

struct ThermostatSettings : ConfigurationSection {
    Property<std::string> switchPeripheral { this, "switch" };
};

struct ThermostatConfig : ConfigurationSection {
    Property<TargetState> overrideState { this, "overrideState" };
};

class Thermostat final
    : public Named,
      public HasConfig<ThermostatConfig> {
public:
    Thermostat(
        const std::string& name,
        const std::shared_ptr<IValve>& switchPeripheral)
        : Named(name)
        , switchPeripheral(switchPeripheral) {
    }

    void configure(const std::shared_ptr<ThermostatConfig>& config) override {
        auto overrideState = config->overrideState.getIfPresent();
        LOGTI(THERMOSTAT, "Thermostat '%s' applying override: %s",
            name.c_str(),
            toString(overrideState));
        switchPeripheral->transitionTo(overrideState);
    }

private:
    const std::shared_ptr<IValve> switchPeripheral;
};

inline FunctionFactory makeFactory() {
    return makeFunctionFactory<Thermostat, ThermostatSettings, ThermostatConfig>(
        "thermostat",
        [](const FunctionInitParameters& params, const std::shared_ptr<ThermostatSettings>& settings) {
            auto switchPeripheral = params.peripheral<IValve>(settings->switchPeripheral.get());
            return std::make_shared<Thermostat>(
                params.name,
                switchPeripheral);
        });
}

}    // namespace farmhub::functions::thermostat
