#pragma once

#include <Pin.hpp>
#include <drivers/Bq27220Driver.hpp>
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

class Mk7Settings
    : public DeviceSettings {
public:
    Mk7Settings()
        : DeviceSettings("mk7") {
    }
};

class UglyDucklingMk7 : public DeviceDefinition<Mk7Settings> {
public:
    UglyDucklingMk7()
        : DeviceDefinition(
            InternalPin::registerPin("STATUS", GPIO_NUM_15),
            InternalPin::registerPin("BOOT", GPIO_NUM_0)) {
        // Switch off strapping pin
        // TODO: Add a LED driver instead
        STATUS2->pinMode(Pin::Mode::Output);
        STATUS2->digitalWrite(1);
    }

    std::shared_ptr<BatteryDriver> createBatteryDriver(const std::shared_ptr<I2CManager>& i2c) override {
        return std::make_shared<Bq27220Driver>(
            i2c,
            SDA,
            SCL,
            BatteryParameters {
                .maximumVoltage = 4100,
                .bootThreshold = 3600,
                .shutdownThreshold = 3000,
            });
    }

protected:
    void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<Mk7Settings>& /*settings*/) override {
        auto motorDriver = Drv8833Driver::create(
            services.pwmManager,
            DAIN1,
            DAIN2,
            DBIN1,
            DBIN2,
            DNFault,
            LOADEN);

        std::map<std::string, std::shared_ptr<PwmMotorDriver>> motors = { { "a", motorDriver->getMotorA() }, { "b", motorDriver->getMotorB() } };

        peripheralManager->registerFactory(valve::makeFactory(motors, ValveControlStrategyType::Latching));
        peripheralManager->registerFactory(door::makeFactory(motors));
    }

private:
    const InternalPinPtr IOA2 = InternalPin::registerPin("A2", GPIO_NUM_1);
    const InternalPinPtr IOA1 = InternalPin::registerPin("A1", GPIO_NUM_2);
    const InternalPinPtr IOA3 = InternalPin::registerPin("A3", GPIO_NUM_3);
    const InternalPinPtr IOB3 = InternalPin::registerPin("B3", GPIO_NUM_4);
    const InternalPinPtr IOB1 = InternalPin::registerPin("B1", GPIO_NUM_5);
    const InternalPinPtr IOB2 = InternalPin::registerPin("B2", GPIO_NUM_6);
    const InternalPinPtr BAT_GPIO = InternalPin::registerPin("BAT_GPIO", GPIO_NUM_8);
    const InternalPinPtr FSPIHD = InternalPin::registerPin("FSPIHD", GPIO_NUM_9);
    const InternalPinPtr FSPICS0 = InternalPin::registerPin("FSPICS0", GPIO_NUM_10);
    const InternalPinPtr FSPID = InternalPin::registerPin("FSPID", GPIO_NUM_11);
    const InternalPinPtr FSPICLK = InternalPin::registerPin("FSPICLK", GPIO_NUM_12);
    const InternalPinPtr FSPIQ = InternalPin::registerPin("FSPIQ", GPIO_NUM_13);
    const InternalPinPtr FSPIWP = InternalPin::registerPin("FSPIWP", GPIO_NUM_14);
    const InternalPinPtr LOADEN = InternalPin::registerPin("LOADEN", GPIO_NUM_16);
    const InternalPinPtr SCL = InternalPin::registerPin("SCL", GPIO_NUM_17);
    const InternalPinPtr SDA = InternalPin::registerPin("SDA", GPIO_NUM_18);
    const InternalPinPtr DMINUS = InternalPin::registerPin("D-", GPIO_NUM_19);
    const InternalPinPtr DPLUS = InternalPin::registerPin("D+", GPIO_NUM_20);
    const InternalPinPtr IOX1 = InternalPin::registerPin("X1", GPIO_NUM_21);
    const InternalPinPtr DBIN1 = InternalPin::registerPin("DBIN1", GPIO_NUM_37);
    const InternalPinPtr DBIN2 = InternalPin::registerPin("DBIN2", GPIO_NUM_38);
    const InternalPinPtr DAIN2 = InternalPin::registerPin("DAIN2", GPIO_NUM_39);
    const InternalPinPtr DAIN1 = InternalPin::registerPin("DAIN1", GPIO_NUM_40);
    const InternalPinPtr DNFault = InternalPin::registerPin("DNFault", GPIO_NUM_41);
    const InternalPinPtr TXD0 = InternalPin::registerPin("TXD0", GPIO_NUM_43);
    const InternalPinPtr RXD0 = InternalPin::registerPin("RXD0", GPIO_NUM_44);
    const InternalPinPtr IOX2 = InternalPin::registerPin("X2", GPIO_NUM_45);
    const InternalPinPtr STATUS2 = InternalPin::registerPin("STATUS2", GPIO_NUM_46);
    const InternalPinPtr IOB4 = InternalPin::registerPin("B4", GPIO_NUM_47);
    const InternalPinPtr IOA4 = InternalPin::registerPin("A4", GPIO_NUM_48);
};

}    // namespace farmhub::devices
