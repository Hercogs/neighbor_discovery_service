#pragma once

#include "common.hpp"
#include "utils.hpp"

#include <net/if.h>

#include <string>
#include <unordered_map>
#include <vector>


// Forward declaration
class SocketManager;
enum class SocketType;
struct DiscoveryMessage;

/**
 * @brief This class stores active neighbors, allows to add and delete them.
 * Inactive neighbor is neighbor which is not updated for 30 seconds.
 * As well maintain Unix socket for communication with CLI to send data.
 */
class NeighborTracker
{
    public:
        NeighborTracker();

        /**
         * @brief Destroy the Neighbor Tracker object. Closes all open sockets.
         */
        ~NeighborTracker();

        /**
         * @brief Add or update neighbor in neighbor list
         * 
         * @param msg data with neighbor data
         */
        void add_neighbor(const DiscoveryMessage& msg);

        /**
         * @brief Removes neighbors which are not active anymore
         */
        void remove_inactive_neighbors();

        /**
         * @brief prints all active neighbors in console
         */
        void print_neighbours() const;

        /**
         * @brief Adds socket manager to manage any sockets
         * @param manager socket manager
         */
        void add_socket_manager(SocketManager* manager);

        /**
         * @brief Checks if this class was initialized succesfully.
         * If Unix server socket was created succesfully.
         * 
         * @return true;
         * @return false 
         */
        bool is_ready() const;
        
        /**
         * @brief Sets up unix server socket object.
         * It includes adding to epoll to receive data. 
         * 
         */
        void setup_unix_server_socket();

        /**
         * @brief Handles incoming msg for any socket for this class
         * 
         * @param fd socket id
         */
        void handle_socket_msg(int fd);

    private:
        /**
         * @brief Sets up unix data socket object to receive data from CLI.
         * It includes adding to epoll to receive data.
         */
        void setup_unix_data_socket();

        /**
         * @brief Prepares and saves data in buffer for unix data socket to send data.
         * 
         * @param only_neighbor_cnt flag to indicate if only active neighbor count or full 
         *                          neighbor list should beincluded
         * @return ssize_t number of bytes written to buffer
         */
        ssize_t write_output_buffer(bool only_neighbor_cnt);

        SocketManager* socket_manager_ {nullptr};       // Socket manager for detecting data for sockets
        int unix_server_socket_ {-1};                   // Unix server socket file descriptor
        int unix_data_socket_ {-1};                     // Unix data socket file descriptor
        std::unordered_map<
            std::string,
            std::unordered_map<
                std::string,
                ConnectionInfo>> neighbor_table_;       // Storage for active neighbors
        char* output_buffer_ {nullptr};                 // Unix data socket outpur buffer
        static constexpr uint64_t inactivity_timeout_ms {30000};    // Timeout in ms when neighbor becames inactive
};

/**
 * @brief Function to generate random data for test purpose
 * 
 * @param number_of_neighbors 
 * @param neighbor_table 
 */
inline void generate_pseudo_data(int number_of_neighbors,
    std::unordered_map<
                std::string,
                std::unordered_map<
                    std::string,
                    ConnectionInfo>>& neighbor_table)
{
    for (int i = 0; i < number_of_neighbors; ++i)
    {
        std::string neighbor_name = generate_guid();

        for (int j = 0; j < 3; ++j)
        { // simulate 3 interfaces per neighbor
            std::string iface_name = "eth" + std::to_string(j);
            ConnectionInfo ci {
                .timestamp = get_current_time_ms()
            };
            memcpy(ci.mac_name, "98:43:fa:ff:a1:fe", sizeof(ci.mac_name));
            memcpy(ci.ipv4, "192.168.0.184", sizeof(ci.ipv4));
            memcpy(ci.ipv6, "fe80::a9b9:a029:76ae:d394", sizeof(ci.ipv6));
            neighbor_table[neighbor_name][iface_name] = ci;
        }
    }
}