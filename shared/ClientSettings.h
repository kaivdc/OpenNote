#pragma once
#include <string>
#include <fstream>
#include <iostream>
#include <array>
#include <cstdint>
#include "BoardStructs.h"

using json = nlohmann::json;

struct Settings {
    std::string clientName = "User";
    std::array<uint8_t, 4> selectionColor = {255, 128, 64, 255};

    // Default Card Dimensions
    float defaultCardWidth = 250.0f;
    float defaultCardHeight = 250.0f;

    // Default Connector width
    float defaultConnectorThickness = 10.0f;

    // Save settings to file
    bool saveToFile(const std::string& filepath = "settings.json") const {
        try {
            json j;
            j["defaultCardWidth"] = defaultCardWidth;
            j["defaultCardHeight"] = defaultCardHeight;
            j["defaultConnectorThickness"] = defaultConnectorThickness;
            j["clientName"] = clientName;
            j["selectionColor"] = selectionColor;

            std::ofstream file(filepath);
            if (!file.is_open()) {
                std::cerr << "[ERROR] : Could not open " << filepath << " for writing.\n";
                return false;
            }

            file << j.dump(4);
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] : Exception while saving settings: " << e.what() << "\n";
            return false;
        }
    }

    // Load settings from file, fallbac to default if no file exists
    bool loadFromFile(const std::string& filepath = "settings.json") {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cout << "[INFO] : No settings file found. Generating default settings.json...\n";
            // Create default file if doesn't exist
            saveToFile(filepath);
            return false;
        }

        try {
            json j;
            file >> j;

            // Safe parsing with fallback to existing defaults if a key is missing
            if (j.contains("defaultCardWidth")) j["defaultCardWidth"].get_to(defaultCardWidth);
            if (j.contains("defaultCardHeight")) j["defaultCardHeight"].get_to(defaultCardHeight);
            if (j.contains("defaultConnectorThickness")) j["defaultConnectorThickness"].get_to(defaultConnectorThickness);
            if (j.contains("clientName") && j["clientName"].is_string()) {
                clientName = j["clientName"].get<std::string>();
            }
            if (j.contains("selectionColor") && j["selectionColor"].is_array() &&
                j["selectionColor"].size() == selectionColor.size()) {
                selectionColor = j["selectionColor"].get<std::array<uint8_t, 4>>();
            }

            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] : Failed to parse settings.json: " << e.what() << ". Using defaults.\n";
            return false;
        }
    }
};
