#pragma once

#include "common.hpp"

#include <net/if.h>

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>


// Forward declaration
class SocketManager;
enum class SocketType;
class NeighborTracker;

/**
 * @brief Structure to store information for active interface
 */
struct InterfaceSocketInfo
{
    SocketType socket_type;         // socket type
    char if_name[IFNAMSIZ];         // interface name (null terminated)
    int if_idx;                     // interface index
    uint8_t mac_raw[MAC_RAW_LEN];   // raw MAC adress
    std::string mac_name;           // MAC name (null terminated)
    std::string ipv4;               // IPv4 name (null terminated)
    std::string ipv6;               // IPv6 name (null terminated)
    bool is_up_and_running {false}; // flag to idicate if interface is up and running
    int socket_fd {-1};             // socket file descriptor
};

/**
 * @brief This class scans all network interfaces, extract ethernet
 * type interfaces and monitor their activity. It publishes discovery
 * messages and listens for them as well to monitor any active neighbor.
 */
class InterfaceTracker
{
    public:
        /**
         * @brief Constructs a new Interface Tracker object
         * 
         * @param guid GUID for host pc
         */
        InterfaceTracker(const std::string& guid);

        /**
         * @brief Destroy the Interface Tracker object. 
         * Close all open sockets.
         */
        ~InterfaceTracker();

        /**
         * @brief Adds socket manager to manage any sockets
         * 
         * @param manager socket manager
         */
        void add_socket_manager(SocketManager* manager);

        /**
         * @brief Adds neighbor tracker to allow updating neighbor table
         * 
         * @param neighbor_tracker neighbor tracker
         */
        void add_neighbor_tracker(NeighborTracker* neighbor_tracker);

        /**
         * @brief Scans all interfaces, check if they are up and running,
         * extracts ethernet type interfaces and update their status for active ones.
         * 
         * @return true if scanning completed;
         */
        bool scan_interfaces();

        /**
         * @brief Close socket for interface
         * 
         * @param fd interface socket file descriptor
         */
        void close_interface_socket(int& fd);

        /**
         * @brief Publishes discovery msgs on all active interfaces
         * 
         */
        void publish_discovery_msg() const;

        /**
         * @brief Handle message on interface socket (incoming discovery message)
         * 
         * @param fd interface socket file descriptor
         */
        void handle_socket_msg(int fd);

        /**
         * @brief prints all active interfaces in console
         */
        void print_if_table() const;

    private:
        /**
         * @brief open socket for specific interface to send and receive messages
         * 
         * @param if_socket information about interface
         * @return int socket file descriptor
         */
        int open_interface_socket(const InterfaceSocketInfo& if_socket);

        /**
         * @brief Removes inactive interface from interface table
         */
        void remove_inactive_interfaces();

        /**
         * @brief Get ipv4 and ipv6 adress for interface. Store None, if not exists
         * 
         * @param if_name interface name
         * @param ipv4 reference to IPv4 storage
         * @param ipv6 reference to IPv6 storage
         */
        void get_ipv4_ipv6(char if_name[IFNAMSIZ],
            std::string& ipv4, std::string& ipv6
        );

        std::unordered_map<std::string, InterfaceSocketInfo> if_table_; // storage for interfaces
        SocketManager* socket_manager_ {nullptr};           
        NeighborTracker* neighbor_tracker_ {nullptr};
        uint8_t buffer_[1024] {0};      // buffer for incoming discovery msgs
        std::string guid_;              // GUID for this host
        static constexpr uint16_t protocol_id {0xfafb};     // Custom protocol id 
        static constexpr int ethernet_header_size {14};     // ethernet header size
};

/**
 * @brief Structure for publishing discovery message.
 */
struct DiscoveryMessage
{
    char guid[GUID_LEN]         {0};    // GUID as string (null terminated)
    char if_name[IFNAMSIZ]      {0};    // interface name (null terminated)
    char mac_name[MAC_STR_LEN]  {0};    // MAC name (null terminated)
    char ipv4[IPV4_MAX_LEN]     {0};    // IPv4 name (null terminated)
    char ipv6[IPV6_MAX_LEN]     {0};    // IPv6 name (null terminated)
    uint8_t checksum;                   // checksum for message

    inline uint8_t calculate_checksum() const
    {
        uint8_t sum = 0;
        for(const char* ptr : {guid, if_name, mac_name, ipv4, ipv6})
        {
            for(size_t i = 0; i < strlen(ptr); ++i)
            {
                sum += static_cast<uint8_t>(ptr[i]);
            }
        }
        return sum;
    }

    inline void update_checksum()
    {
        checksum = calculate_checksum();
    }

    inline bool verify_checksum() const
    {
        return checksum == calculate_checksum();
    }
};


