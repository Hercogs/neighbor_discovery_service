#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>


#define MAC_RAW_LEN     6   // Length of MAC in raw bytes
#define MAC_STR_LEN     18  // Length of MAC as string + '\0'
#define IPV4_MAX_LEN    16  // Length of IPv4 as string + '\0'
#define IPV6_MAX_LEN    46  // Length of IPv6 as string + '\0'
#define GUID_LEN        37  // Length of GUID as string + '\0'

// Unix socket path for background service and CLI communication
constexpr const char* unix_socket_path ="/tmp/unix_discovery_socket";

/**
 * @brief Structure to specify neighbor data.
 */
struct ConnectionInfo
{
    char mac_name[MAC_STR_LEN]  {0};            // MAC name (null terminated)
    char ipv4[IPV4_MAX_LEN]     {0};            // IPv4 name (null terminated)
    char ipv6[IPV6_MAX_LEN]     {0};            // IPv6 name (null terminated)
    uint64_t timestamp;                         // Time in ms since epoch when neighbor data were refreshed
};

/**
 * @brief Structure to specify type of data being requested.
 */
struct UnixSocketRequest
{
    bool request_only_neighbor_cnt {false};     // flag to request only neighbor count 
};
