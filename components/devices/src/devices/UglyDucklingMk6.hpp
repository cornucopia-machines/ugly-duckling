#pragma once

#include <map>
#include <memory>

#include <MacAddress.hpp>
#include <Pin.hpp>
#include <drivers/BatteryDriver.hpp>
#include <drivers/Drv8833Driver.hpp>
#include <drivers/LedDriver.hpp>

#include <peripherals/Peripheral.hpp>
#include <peripherals/door/Door.hpp>
#include <peripherals/valve/ValveFactory.hpp>

#include <devices/DeviceDefinition.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals::door;
using namespace farmhub::peripherals::valve;

namespace farmhub::devices {

class Mk6Settings
    : public DeviceSettings {
public:
    Mk6Settings()
        : DeviceSettings("mk6") {
    }
};

class UglyDucklingMk6Base : public DeviceDefinition<Mk6Settings> {
public:
    UglyDucklingMk6Base()
        : DeviceDefinition(
            InternalPin::registerPin("STATUS", GPIO_NUM_2),
            InternalPin::registerPin("BOOT", GPIO_NUM_0)) {
        // Switch off strapping pin
        // TODO(lptr): Add a LED driver instead
        LEDA_RED->pinMode(Pin::Mode::Output);
        LEDA_RED->digitalWrite(1);
    }

    std::shared_ptr<BatteryDriver> createBatteryDriver(const std::shared_ptr<I2CManager>& /*i2c*/) override {
        return std::make_shared<AnalogBatteryDriver>(
            BATTERY,
            1.2424,
            BatteryParameters {
                .maximumVoltage = 4100,
                .bootThreshold = 3600,
                .shutdownThreshold = 3400,
            });
    }

protected:
    virtual PinPtr motorNSleepPin() const = 0;

    void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<Mk6Settings>& /*settings*/) override {
        auto motorDriver = Drv8833Driver::create(
            services.pwmManager,
            AIN1,
            AIN2,
            BIN1,
            BIN2,
            NFault,
            motorNSleepPin(),
            true);

        std::map<std::string, std::shared_ptr<PwmMotorDriver>> motors = { { "a", motorDriver->getMotorA() }, { "b", motorDriver->getMotorB() } };

        peripheralManager->registerFactory(valve::makeFactory(motors, ValveControlStrategyType::Latching));
        peripheralManager->registerFactory(door::makeFactory(motors));
    }

protected:
    const InternalPinPtr BATTERY = InternalPin::registerPin("BATTERY", GPIO_NUM_1);
    const InternalPinPtr STATUS2 = InternalPin::registerPin("STATUS2", GPIO_NUM_4);
    const InternalPinPtr IOB1 = InternalPin::registerPin("B1", GPIO_NUM_5);
    const InternalPinPtr IOA1 = InternalPin::registerPin("A1", GPIO_NUM_6);
    const InternalPinPtr DIPROPI = InternalPin::registerPin("DIPROPI", GPIO_NUM_7);
    const InternalPinPtr IOA2 = InternalPin::registerPin("A2", GPIO_NUM_15);
    const InternalPinPtr AIN1 = InternalPin::registerPin("AIN1", GPIO_NUM_16);
    const InternalPinPtr AIN2 = InternalPin::registerPin("AIN2", GPIO_NUM_17);
    const InternalPinPtr BIN2 = InternalPin::registerPin("BIN2", GPIO_NUM_18);
    const InternalPinPtr BIN1 = InternalPin::registerPin("BIN1", GPIO_NUM_8);
    const InternalPinPtr DMINUS = InternalPin::registerPin("D-", GPIO_NUM_19);
    const InternalPinPtr DPLUS = InternalPin::registerPin("D+", GPIO_NUM_20);
    const InternalPinPtr LEDA_RED = InternalPin::registerPin("LEDA_RED", GPIO_NUM_46);
    const InternalPinPtr LEDA_GREEN = InternalPin::registerPin("LEDA_GREEN", GPIO_NUM_9);
    const InternalPinPtr NFault = InternalPin::registerPin("NFault", GPIO_NUM_11);
    const InternalPinPtr BTN1 = InternalPin::registerPin("BTN1", GPIO_NUM_12);
    const InternalPinPtr BTN2 = InternalPin::registerPin("BTN2", GPIO_NUM_13);
    const InternalPinPtr IOC4 = InternalPin::registerPin("C4", GPIO_NUM_14);
    const InternalPinPtr IOC3 = InternalPin::registerPin("C3", GPIO_NUM_21);
    const InternalPinPtr IOC2 = InternalPin::registerPin("C2", GPIO_NUM_47);
    const InternalPinPtr IOC1 = InternalPin::registerPin("C1", GPIO_NUM_48);
    const InternalPinPtr IOB2 = InternalPin::registerPin("B2", GPIO_NUM_45);
    const InternalPinPtr SDA = InternalPin::registerPin("SDA", GPIO_NUM_35);
    const InternalPinPtr SCL = InternalPin::registerPin("SCL", GPIO_NUM_36);
    const InternalPinPtr LEDB_GREEN = InternalPin::registerPin("LEDB_GREEN", GPIO_NUM_37);
    const InternalPinPtr LEDB_RED = InternalPin::registerPin("LEDB_RED", GPIO_NUM_38);
    const InternalPinPtr TCK = InternalPin::registerPin("TCK", GPIO_NUM_39);
    const InternalPinPtr TDO = InternalPin::registerPin("TDO", GPIO_NUM_40);
    const InternalPinPtr TDI = InternalPin::registerPin("TDI", GPIO_NUM_41);
    const InternalPinPtr TMS = InternalPin::registerPin("TMS", GPIO_NUM_42);
    const InternalPinPtr RXD0 = InternalPin::registerPin("RXD0", GPIO_NUM_44);
    const InternalPinPtr TXD0 = InternalPin::registerPin("TXD0", GPIO_NUM_43);
    // Available on MK6 Rev3+
    const InternalPinPtr LOADEN = InternalPin::registerPin("LOADEN", GPIO_NUM_10);
};

// MAC prefix 0x34:0x85:0x18
class UglyDucklingMk6Rev1 : public UglyDucklingMk6Base {
protected:
    PinPtr motorNSleepPin() const override {
        return IOC2;
    }
};

// MAC prefix 0xec:0xda:0x3b:0x5b
class UglyDucklingMk6Rev2 : public UglyDucklingMk6Base {
protected:
    PinPtr motorNSleepPin() const override {
        return IOC2;
    }
};

// All other known MK6 MAC ranges
class UglyDucklingMk6Rev3 : public UglyDucklingMk6Base {
protected:
    PinPtr motorNSleepPin() const override {
        return LOADEN;
    }
};

}    // namespace farmhub::devices
