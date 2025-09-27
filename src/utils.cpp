#include "utils.hpp"

#include <random>
#include <sstream>
#include <iomanip>
#include <iostream>


std::string generate_guid()
{
    std::random_device rsg;         // Seed generator
    std::mt19937 mt_engine(rsg());  // Random number engine
    std::uniform_int_distribution<uint32_t> dist32;
    std::uniform_int_distribution<uint16_t> dist16;

    std::ostringstream oss;

    oss << std::hex << std::setfill('0');

    // Generate guid 8-4-4-4-12
    oss << std::setw(8) << dist32(mt_engine) << '-';
    oss << std::setw(4) << dist16(mt_engine) << '-';
    oss << std::setw(4) << dist16(mt_engine) << '-';
    oss << std::setw(4) << dist16(mt_engine) << '-';
    oss << std::setw(8) << dist32(mt_engine);
    oss << std::setw(4) << dist16(mt_engine);

    return oss.str();
}
