#pragma once

#include <Pin.hpp>
#include <drivers/BatteryDriver.hpp>
#include <drivers/Drv8874Driver.hpp>
#include <drivers/LedDriver.hpp>

#include <peripherals/door/Door.hpp>
#include <peripherals/valve/ValveFactory.hpp>

#include <devices/DeviceDefinition.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals::door;
using namespace farmhub::peripherals::valve;

namespace farmhub::devices {

class Mk5Settings
    : public DeviceSettings {
public:
    Mk5Settings()
        : DeviceSettings("mk5") {
    }
};

class UglyDucklingMk5 : public DeviceDefinition<Mk5Settings> {
public:
    UglyDucklingMk5()
        : DeviceDefinition(
            InternalPin::registerPin("STATUS", GPIO_NUM_2),
            InternalPin::registerPin("BOOT", GPIO_NUM_0)) {
    }

protected:
    void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<Mk5Settings>& /*settings*/) override {
        auto motorA = std::make_shared<Drv8874Driver>(
            services.pwmManager,
            AIN1,
            AIN2,
            AIPROPI,
            NFault,
            NSLEEP);

        auto motorB = std::make_shared<Drv8874Driver>(
            services.pwmManager,
            BIN1,
            BIN2,
            BIPROPI,
            NFault,
            NSLEEP);

        std::map<std::string, std::shared_ptr<PwmMotorDriver>> motors = { { "a", motorA }, { "b", motorB } };

        peripheralManager->registerFactory(valve::makeFactory(motors, ValveControlStrategyType::Latching));
        peripheralManager->registerFactory(door::makeFactory(motors));
    }

private:
    const InternalPinPtr BATTERY = InternalPin::registerPin("BATTERY", GPIO_NUM_1);
    const InternalPinPtr AIPROPI = InternalPin::registerPin("AIPROPI", GPIO_NUM_4);
    const InternalPinPtr IOA1 = InternalPin::registerPin("A1", GPIO_NUM_5);
    const InternalPinPtr IOA2 = InternalPin::registerPin("A2", GPIO_NUM_6);
    const InternalPinPtr BIPROPI = InternalPin::registerPin("BIPROPI", GPIO_NUM_7);
    const InternalPinPtr IOB1 = InternalPin::registerPin("B1", GPIO_NUM_15);
    const InternalPinPtr AIN1 = InternalPin::registerPin("AIN1", GPIO_NUM_16);
    const InternalPinPtr AIN2 = InternalPin::registerPin("AIN2", GPIO_NUM_17);
    const InternalPinPtr BIN1 = InternalPin::registerPin("BIN1", GPIO_NUM_18);
    const InternalPinPtr BIN2 = InternalPin::registerPin("BIN2", GPIO_NUM_8);
    const InternalPinPtr DMINUS = InternalPin::registerPin("D-", GPIO_NUM_19);
    const InternalPinPtr DPLUS = InternalPin::registerPin("D+", GPIO_NUM_20);
    const InternalPinPtr IOB2 = InternalPin::registerPin("B2", GPIO_NUM_9);
    const InternalPinPtr NSLEEP = InternalPin::registerPin("NSLEEP", GPIO_NUM_10);
    const InternalPinPtr NFault = InternalPin::registerPin("NFault", GPIO_NUM_11);
    const InternalPinPtr IOC4 = InternalPin::registerPin("C4", GPIO_NUM_12);
    const InternalPinPtr IOC3 = InternalPin::registerPin("C3", GPIO_NUM_13);
    const InternalPinPtr IOC2 = InternalPin::registerPin("C2", GPIO_NUM_14);
    const InternalPinPtr IOC1 = InternalPin::registerPin("C1", GPIO_NUM_21);
    const InternalPinPtr IOD4 = InternalPin::registerPin("D4", GPIO_NUM_47);
    const InternalPinPtr IOD3 = InternalPin::registerPin("D3", GPIO_NUM_48);
    const InternalPinPtr SDA = InternalPin::registerPin("SDA", GPIO_NUM_35);
    const InternalPinPtr SCL = InternalPin::registerPin("SCL", GPIO_NUM_36);
    const InternalPinPtr IOD1 = InternalPin::registerPin("D1", GPIO_NUM_37);
    const InternalPinPtr IOD2 = InternalPin::registerPin("D2", GPIO_NUM_38);
    const InternalPinPtr TCK = InternalPin::registerPin("TCK", GPIO_NUM_39);
    const InternalPinPtr TDO = InternalPin::registerPin("TDO", GPIO_NUM_40);
    const InternalPinPtr TDI = InternalPin::registerPin("TDI", GPIO_NUM_41);
    const InternalPinPtr TMS = InternalPin::registerPin("TMS", GPIO_NUM_42);
    const InternalPinPtr RXD0 = InternalPin::registerPin("RXD0", GPIO_NUM_44);
    const InternalPinPtr TXD0 = InternalPin::registerPin("TXD0", GPIO_NUM_43);
};

}    // namespace farmhub::devices
