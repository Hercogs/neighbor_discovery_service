#pragma once

#include <chrono>
#include <string>


/**
 * @brief Generates global uniqe identifier (GUID) in form:
 * 8-4-4-4-12 (hex chars)
 * 
 * @return std::string A string representing generated GUID
 */
std::string generate_guid();

/**
 * @brief Get the current time (ms) since epoch
 * 
 * @return uint64_t Number of milliseconds since epoch
 */
inline uint64_t get_current_time_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}