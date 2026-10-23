#include "joycond_config.h"

#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{
std::string trim(std::string const &s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::optional<bool> parse_bool(std::string const &value) {
    if (value == "on" || value == "1" ||
        value == "true" || value == "t" || value == "T" ||
        value == "yes" || value == "y" || value == "Y")
        return true;
    else if (value == "off" || value == "0" ||
        value == "false" || value == "f" || value == "F" ||
        value == "no" || value == "n" || value == "N")
        return false;
    else
        return std::nullopt;
}
}

joycond_config load_config(std::string const &path)
{
    joycond_config config;

    std::ifstream in(path);
    if (!in)
        return config;

    std::string line;
    int line_num = 0;
    while (std::getline(in, line)) {
        line_num++;
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            std::cerr << "Config " << path << " line " << line_num
                      << ": expected 'key = value'\n";
            continue;
        }
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (key == "swap_ab_xy") {
            std::optional<bool> swap_ab_xy_parsed = parse_bool(value);
            if (swap_ab_xy_parsed)
                config.swap_ab_xy = *swap_ab_xy_parsed;
            else
                std::cerr << "Config " << path << " line " << line_num
                          << ": swap_ab_xy expects true|false, got '" << value << "'\n";
        } else if (key == "pair_rumble_mode") {
            if (value == "both")
                config.pair_rumble_mode = CombinedRumbleMode::Both;
            else if (value == "left")
                config.pair_rumble_mode = CombinedRumbleMode::Left;
            else if (value == "right")
                config.pair_rumble_mode = CombinedRumbleMode::Right;
            else if (value == "off")
                config.pair_rumble_mode = CombinedRumbleMode::Off;
            else
                std::cerr << "Config " << path << " line " << line_num
                          << ": pair_rumble_mode expects both|left|right|off, got '" << value << "'\n";
        } else if (key == "rumble_gain") {
            try {
                size_t pos;
                double gain = std::stod(value, &pos);
                if (pos != value.size())
                    throw std::invalid_argument(value);
                if (gain < 0.0 || gain > 1.0)
                    throw std::out_of_range(value);
                config.rumble_gain = gain;
            } catch (std::exception const &) {
                std::cerr << "Config " << path << " line " << line_num
                          << ": rumble_gain expects a number in [0.0, 1.0], got '" << value << "'\n";
            }
        } else {
            std::cerr << "Config " << path << " line " << line_num
                      << ": unknown key '" << key << "'\n";
        }
    }

    return config;
}
