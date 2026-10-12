#pragma once
#include <charconv>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "PlutoGE/assets/Project.h"

namespace PlutoGE
{
    struct ServerOptions
    {
        std::filesystem::path project;
        std::string scene;
        unsigned tickRate = 60;
        bool tickRateSpecified = false;
        unsigned ticks = 0; // Zero runs until shutdown; nonzero supports automation.
        std::vector<std::string> scripts;
        std::vector<assets::ServerEnvironmentVariable> environment;
        static ServerOptions Parse(int argc, char **argv)
        {
            ServerOptions result;
            for (int i = 1; i < argc; ++i)
            {
                const std::string_view option(argv[i]);
                if (i + 1 == argc) throw std::invalid_argument("Missing value for " + std::string(option));
                const std::string value(argv[++i]);
                if (option == "--project") result.project = value;
                else if (option == "--scene") result.scene = value;
                else if (option == "--script") result.scripts.push_back(value);
                else if (option == "--env")
                {
                    const auto equals = value.find('=');
                    if (equals == std::string::npos || !assets::IsServerEnvironmentName(value.substr(0, equals)))
                        throw std::invalid_argument("Expected --env NAME=VALUE; PLUTOGE_HEADLESS is reserved");
                    result.environment.push_back({value.substr(0, equals), value.substr(equals + 1)});
                }
                else if (option == "--tick-rate" || option == "--ticks")
                {
                    unsigned number = 0;
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                        (option == "--tick-rate" && (number < 10 || number > 240)))
                        throw std::invalid_argument("Invalid value for " + std::string(option));
                    if (option == "--ticks") result.ticks = number;
                    else { result.tickRate = number; result.tickRateSpecified = true; }
                }
                else throw std::invalid_argument("Unknown server option: " + std::string(option));
            }
            return result;
        }
        void ApplyDefaults(const assets::ProjectServerSettings &defaults)
        {
            if (scene.empty()) scene = defaults.startupScene;
            if (scripts.empty()) scripts = defaults.scripts;
            if (!tickRateSpecified) tickRate = static_cast<unsigned>(defaults.tickRate);
            if (scene.empty() || scripts.empty() || tickRate < 10 || tickRate > 240)
                throw std::invalid_argument("Configure a server scene, script allow list and tick rate in Project Settings, or supply --scene, --script and --tick-rate.");
        }
    };
}
