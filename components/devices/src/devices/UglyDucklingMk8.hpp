#pragma once

#include <memory>

#include <MacAddress.hpp>
#include <Pin.hpp>
#include <drivers/Bq27220Driver.hpp>
#include <drivers/Drv8848Driver.hpp>
#include <drivers/Ina219Driver.hpp>
#include <drivers/LedDriver.hpp>

#include <peripherals/Peripheral.hpp>
#include <peripherals/door/Door.hpp>
#include <peripherals/valve/ValveFactory.hpp>

#include <devices/DeviceDefinition.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals::door;
using namespace farmhub::peripherals::valve;

namespace farmhub::devices {

class Mk8Settings
    : public DeviceSettings {
public:
    Mk8Settings()
        : DeviceSettings("mk8") {
    }
};

class UglyDucklingMk8Base : public DeviceDefinition<Mk8Settings> {
public:
    UglyDucklingMk8Base()
        : DeviceDefinition(
            InternalPin::registerPin("STATUS", GPIO_NUM_45),
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
                .bootThreshold = 3500,
                .shutdownThreshold = 3300,
            });
    }

protected:
    void registerMotorAndValves(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services) {
        auto motorDriver = Drv8848Driver::create(
            services.pwmManager,
            DAIN1,
            DAIN2,
            DBIN1,
            DBIN2,
            NFAULT,
            LOADEN);

        std::map<std::string, std::shared_ptr<PwmMotorDriver>> motors = { { "a", motorDriver->getMotorA() }, { "b", motorDriver->getMotorB() } };

        peripheralManager->registerFactory(valve::makeFactory(motors, ValveControlStrategyType::Latching));
        peripheralManager->registerFactory(door::makeFactory(motors));
    }

protected:
    // Internal I2C
    const InternalPinPtr SDA = InternalPin::registerPin("SDA", GPIO_NUM_1);
    const InternalPinPtr SCL = InternalPin::registerPin("SCL", GPIO_NUM_2);
    // Watchdog interrupt
    const InternalPinPtr WDI = InternalPin::registerPin("WDI", GPIO_NUM_3);
    // Port B pins
    const InternalPinPtr IOB3 = InternalPin::registerPin("B3", GPIO_NUM_4);
    const InternalPinPtr IOB1 = InternalPin::registerPin("B1", GPIO_NUM_5);
    const InternalPinPtr IOB2 = InternalPin::registerPin("B2", GPIO_NUM_6);
    const InternalPinPtr IOB4 = InternalPin::registerPin("B4", GPIO_NUM_7);
    // Battery fuel gauge interrupt
    const InternalPinPtr BAT_GAUGE = InternalPin::registerPin("BAT_GAUGE", GPIO_NUM_8);
    // SPI for e-ink display
    const InternalPinPtr SBUSY = InternalPin::registerPin("SBUSY", GPIO_NUM_9);
    const InternalPinPtr SCS = InternalPin::registerPin("SCS", GPIO_NUM_10);
    const InternalPinPtr SSDI = InternalPin::registerPin("SSDI", GPIO_NUM_11);
    const InternalPinPtr SSCLK = InternalPin::registerPin("SSCLK", GPIO_NUM_12);
    const InternalPinPtr SRES = InternalPin::registerPin("SRES", GPIO_NUM_13);
    const InternalPinPtr SDC = InternalPin::registerPin("SDC", GPIO_NUM_14);
    // Port A pins
    const InternalPinPtr IOA3 = InternalPin::registerPin("A3", GPIO_NUM_15);
    const InternalPinPtr IOA1 = InternalPin::registerPin("A1", GPIO_NUM_16);
    const InternalPinPtr IOA2 = InternalPin::registerPin("A2", GPIO_NUM_17);
    const InternalPinPtr IOA4 = InternalPin::registerPin("A4", GPIO_NUM_18);
    // USB
    const InternalPinPtr DMINUS = InternalPin::registerPin("D-", GPIO_NUM_19);
    const InternalPinPtr DPLUS = InternalPin::registerPin("D+", GPIO_NUM_20);
    // Motor control pins
    const InternalPinPtr DAIN2 = InternalPin::registerPin("DAIN2", GPIO_NUM_35);
    const InternalPinPtr DAIN1 = InternalPin::registerPin("DAIN1", GPIO_NUM_36);
    const InternalPinPtr DBIN1 = InternalPin::registerPin("DBIN1", GPIO_NUM_37);
    const InternalPinPtr DBIN2 = InternalPin::registerPin("DBIN2", GPIO_NUM_38);
    // Debug
    const InternalPinPtr TCK = InternalPin::registerPin("TCK", GPIO_NUM_39);
    const InternalPinPtr TDO = InternalPin::registerPin("TDO", GPIO_NUM_40);
    const InternalPinPtr TDI = InternalPin::registerPin("TDI", GPIO_NUM_41);
    const InternalPinPtr TMS = InternalPin::registerPin("TMS", GPIO_NUM_42);
    // UART
    const InternalPinPtr RXD0 = InternalPin::registerPin("RXD0", GPIO_NUM_43);
    const InternalPinPtr TXD0 = InternalPin::registerPin("TXD0", GPIO_NUM_44);
    // Status LEDs
    const InternalPinPtr STATUS2 = InternalPin::registerPin("STATUS2", GPIO_NUM_46);
    // Enable / disable external load
    const InternalPinPtr LOADEN = InternalPin::registerPin("LOADEN", GPIO_NUM_47);
    // Motor fault pin
    const InternalPinPtr NFAULT = InternalPin::registerPin("NFAULT", GPIO_NUM_48);
};

// MAC prefix 0x98:0xa3:0x16:0x1a — INA219 omitted due to hardware fault on these units
class UglyDucklingMk8Rev1 : public UglyDucklingMk8Base {
protected:
    void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<Mk8Settings>& /*settings*/) override {
        registerMotorAndValves(peripheralManager, services);
    }
};

// All other known MK8 MAC ranges — INA219 included
class UglyDucklingMk8Rev2 : public UglyDucklingMk8Base {
protected:
    void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<Mk8Settings>& /*settings*/) override {
        registerMotorAndValves(peripheralManager, services);

        ina219 = std::make_shared<Ina219Driver>(
            services.i2c,
            I2CConfig {
                .address = Ina219Driver::DEFAULT_ADDRESS,
                .sda = SDA,
                .scl = SCL,
            },
            Ina219Parameters {
                .uRange = INA219_BUS_RANGE_16V,
                .gain = INA219_GAIN_0_125,
                .uResolution = INA219_RES_12BIT_1S,
                .iResolution = INA219_RES_12BIT_1S,
                .mode = INA219_MODE_CONT_SHUNT_BUS,
                .shuntMilliOhm = 50,
            });
    }

private:
    std::shared_ptr<Ina219Driver> ina219;
};

}    // namespace farmhub::devices
