#pragma once

#include "Configuration.hpp"
#include "FileSystem.hpp"

namespace farmhub::kernel {

template <std::derived_from<ConfigurationSection> TConfiguration>
class ConfigurationFile {
public:
    ConfigurationFile(const std::shared_ptr<FileSystem>& fs, const std::string& path, std::shared_ptr<TConfiguration> config)
        : path(path)
        , config(std::move(config)) {
        if (!fs->exists(path)) {
            LOGD("The configuration file '%s' was not found, falling back to defaults",
                path.c_str());
        } else {
            auto contents = fs->readAll(path);
            if (!contents.has_value()) {
                throw ConfigurationException("Cannot open config file " + path);
            }

            JsonDocument json;
            DeserializationError error = deserializeJson(json, contents.value());
            switch (error.code()) {
                case DeserializationError::Code::Ok:
                    break;
                case DeserializationError::Code::EmptyInput:
                    LOGD("The configuration file '%s' is empty, falling back to defaults",
                        path.c_str());
                    break;
                default:
                    throw ConfigurationException("Cannot open config file " + path + " (" + std::string(error.c_str()) + ")");
            }
            update(json.as<JsonObject>());
            LOGD("Effective configuration for '%s': %s",
                path.c_str(), toString().c_str());
        }
        onUpdate([fs, path](const JsonObject& json) {
            std::string contents;
            serializeJson(json, contents);
            bool success = fs->writeAll(path, contents) != 0U;
            if (!success) {
                throw ConfigurationException("Cannot write config file " + path);
            }
        });
    }

    void reset() {
        config->reset();
    }

    void update(const JsonObject& json) {
        config->load(json);

        for (auto& callback : callbacks) {
            callback(json);
        }
    }

    void onUpdate(const std::function<void(const JsonObject&)>& callback) {
        callbacks.push_back(callback);
    }

    void store(JsonObject& json) const {
        config->store(json);
    }

    std::shared_ptr<TConfiguration> getConfig() const {
        return config;
    }

    std::string toString() {
        JsonDocument json;
        auto root = json.to<JsonObject>();
        store(root);
        std::string jsonString;
        serializeJson(json, jsonString);
        return jsonString;
    }

private:
    const std::string path;
    std::shared_ptr<TConfiguration> config;
    std::list<std::function<void(const JsonObject&)>> callbacks;
};

}    // namespace farmhub::kernel
