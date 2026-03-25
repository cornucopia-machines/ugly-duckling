#pragma once

#include <memory>
#include <utility>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <I2CManager.hpp>
#include <peripherals/I2CSettings.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/api/ISoilMoistureSensor.hpp>
#include <utils/DebouncedMeasurement.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::environment {

class Fdc1004SoilMoistureSensorSettings
    : public I2CSettings {
public:
    // Capacitance values in femto-farads for calibration —
    // measure your specific probe in dry air and submerged in water to get these values.
    Property<int32_t> air { this, "air", 0 };
    Property<int32_t> water { this, "water", 50000 };
};

class Fdc1004SoilMoistureSensor final
    : public api::ISoilMoistureSensor,
      public Peripheral {
public:
    Fdc1004SoilMoistureSensor(
        const std::string& name,
        const std::shared_ptr<I2CManager>& i2c,
        const I2CConfig& config,
        int32_t airValue,
        int32_t waterValue)
        : Peripheral(name)
        , device(i2c->createDevice(name, config))
        , airValue(airValue)
        , waterValue(waterValue) {

        LOGTI(ENV, "Initializing FDC1004 soil moisture sensor '%s' with %s; air: %ld fF, water: %ld fF",
            name.c_str(), config.toString().c_str(), (long) airValue, (long) waterValue);

        auto manufacturerId = device->readRegWord(0xFE);
        auto deviceId = device->readRegWord(0xFF);

        LOGTD(ENV, "FDC1004 Manufacturer ID: 0x%04x, Device ID: 0x%04x",
            manufacturerId, deviceId);
    }

    Percent getMoisture() override {
        return measurement.getValue();
    }

private:
    std::shared_ptr<I2CDevice> device;
    const int32_t airValue;
    const int32_t waterValue;

    // Register addresses (from FDC1004 datasheet, Table 6)
    static constexpr uint8_t MEAS_CONFIG[] = { 0x08, 0x09, 0x0A, 0x0B };
    static constexpr uint8_t MEAS_MSB[] = { 0x00, 0x02, 0x04, 0x06 };
    static constexpr uint8_t MEAS_LSB[] = { 0x01, 0x03, 0x05, 0x07 };
    static constexpr uint8_t FDC_REGISTER = 0x0C;

    // Capacitance conversion constants (from FDC1004 datasheet)
    static constexpr int32_t ATTOFARADS_UPPER_WORD = 457;    // aF per LSB of MSB result word
    static constexpr int32_t FEMTOFARADS_CAPDAC = 3028;      // fF per LSB of CAPDAC
    static constexpr uint8_t CAPDAC_MAX = 0x1F;
    static constexpr int16_t UPPER_BOUND = 0x4000;
    static constexpr int16_t LOWER_BOUND = -UPPER_BOUND;

    // Measurement index 1..4 maps to register groups
    enum class Meas : uint8_t {
        M1 = 1,
        M2,
        M3,
        M4,
    };

    // Positive input CINx for CHA
    enum class Cin : uint8_t {
        CIN1 = 0,
        CIN2 = 1,
        CIN3 = 2,
        CIN4 = 3,
        C_NONE = 4,
    };

    // Sample rates per datasheet (FDC_CONF RATE bits [11:10])
    enum class Rate : uint16_t {
        SPS_100 = 0b01,
        SPS_200 = 0b10,
        SPS_400 = 0b11,
    };

    // CAPDAC auto-ranging state for channel CIN1 / measurement M1
    uint8_t capdac = 0;

    void configureSingleChannelMeasurement(Meas meas, Cin channel, uint8_t capdacValue) {
        return configureMeasurement(meas, channel, Cin::C_NONE, capdacValue);
    }

    void configureMeasurement(Meas meas, Cin channel1, Cin channel2, uint8_t capdacValue) {
        uint16_t config = 0;
        config |= static_cast<uint16_t>(channel1) << 13;      // CHA: positive input
        config |= static_cast<uint16_t>(channel2) << 10;     // CHB: negative input
        config |= static_cast<uint16_t>(capdacValue) << 5;   // CAPDAC offset value
        device->writeRegWord(MEAS_CONFIG[static_cast<uint8_t>(meas) - 1], config);
    }

    void triggerMeasurement(Meas meas, Rate rate) {
        uint16_t trigger = 0;
        trigger |= static_cast<uint16_t>(rate) << 10;
        trigger |= (1 << (8 - static_cast<uint8_t>(meas)));    // bit 7 = M1, bit 6 = M2, ...
        device->writeRegWord(FDC_REGISTER, trigger);
    }

    bool isMeasurementDone(Meas meas) {
        uint16_t fdcReg = device->readRegWord(FDC_REGISTER);
        return fdcReg & (1 << (4 - static_cast<uint8_t>(meas)));    // bit 3 = M1, bit 2 = M2, ...
    }

    // Returns capacitance in femtofarads, or nullopt while the CAPDAC is being adjusted.
    std::optional<int32_t> measureCapacitanceInFemtoFarads(Cin channel) {
        static constexpr Meas meas = Meas::M1;

        configureSingleChannelMeasurement(meas, channel, capdac);

        auto tries = CAPDAC_MAX + 1;
        while (tries-- > 0) {
            triggerMeasurement(meas, Rate::SPS_100);
            Task::delay(11ms);    // 100 Hz → 10 ms/sample; 11 ms for safety

            if (!isMeasurementDone(meas)) {
                LOGTW(ENV, "FDC1004 measurement not completed");
                return std::nullopt;
            }

            // TODO Also read LSB?
            uint16_t msb = device->readRegWord(MEAS_MSB[static_cast<uint8_t>(meas) - 1]);
            int16_t value = static_cast<int16_t>(msb);

            LOGTV(ENV, "FDC1004 measured %d at capdac = %d", value, capdac);

            // Auto-range the CAPDAC so the measurement stays within the representable window
            if (value >= LOWER_BOUND && value <= UPPER_BOUND) {
                // Within representable range, convert to capacitance and return
                int32_t capacitance = (ATTOFARADS_UPPER_WORD * static_cast<int32_t>(value)) / 1000;    // fF
                capacitance += FEMTOFARADS_CAPDAC * static_cast<int32_t>(capdac);
                return capacitance;
            }
            if (value > UPPER_BOUND) {
                if (capdac < CAPDAC_MAX) {
                    capdac++;
                    LOGTV(ENV, "FDC1004 measurement above representable range, increasing CAPDAC to %d", capdac);
                } else {
                    LOGTW(ENV, "FDC1004 measurement above representable range even at max CAPDAC");
                    return std::nullopt;
                }
            } else if (value < LOWER_BOUND) {
                if (capdac > 0) {
                    capdac--;
                    LOGTV(ENV, "FDC1004 measurement below representable range, decreasing CAPDAC to %d", capdac);
                } else {
                    LOGTW(ENV, "FDC1004 measurement below representable range even at min CAPDAC");
                    return std::nullopt;
                }
            }
        }
        LOGTW(ENV, "FDC1004 measurement did not complete after %d retries", CAPDAC_MAX + 1);
        return std::nullopt;
    }

    utils::DebouncedMeasurement<Percent> measurement {
        [this](const utils::DebouncedParams<Percent> params) -> std::optional<Percent> {
            auto capacitance1 = measureCapacitanceInFemtoFarads(Cin::CIN1);
            if (!capacitance1) {
                return std::nullopt;
            }
            auto capacitance2 = measureCapacitanceInFemtoFarads(Cin::CIN2);
            if (!capacitance2) {
                return std::nullopt;
            }
            auto capacitance = (*capacitance1 + *capacitance2) / 2;
            LOGTV(ENV, "FDC1004 capacitance: %ld fF (CH1 = %ld fF, CH2 = %ld fF, capdac=%d)",
                capacitance, (long) *capacitance1, (long) *capacitance2, capdac);

            const double run = waterValue - airValue;
            const double rise = 100;
            const double delta = capacitance - airValue;
            return (delta * rise) / run;
        },
        1s,
        NAN
    };
};

inline PeripheralFactory makeFactoryForFdc1004SoilMoisture() {
    return makePeripheralFactory<ISoilMoistureSensor, Fdc1004SoilMoistureSensor, Fdc1004SoilMoistureSensorSettings>(
        "environment:fdc1004-soil-moisture",
        "environment",
        [](PeripheralInitParameters& params, const std::shared_ptr<Fdc1004SoilMoistureSensorSettings>& settings) {
            I2CConfig i2cConfig = settings->parse(0x50);    // Fixed address for FDC1004
            auto sensor = std::make_shared<Fdc1004SoilMoistureSensor>(
                params.name,
                params.services.i2c,
                i2cConfig,
                settings->air.get(),
                settings->water.get());
            params.registerFeature("moisture", [sensor](JsonObject& telemetryJson) {
                telemetryJson["value"] = sensor->getMoisture();
            });
            return sensor;
        });
}

}    // namespace farmhub::peripherals::environment
