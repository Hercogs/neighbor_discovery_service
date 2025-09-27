#pragma once

#include "utils.hpp"

#include <cstring>

#include <net/if.h>

#define MAX_EPOLL_EVENTS 16     // Maximum number of events epoll returns
                                // in epoll_wait()


// Forward declarations
class InterfaceTracker;
class NeighborTracker;

/**
 * @brief Structure to indicate socket type for managing it
 */
enum class SocketType
{
    Interface,                  // Interface socket for active interface
    Unix,                       // Unix socket for CLI and neighbor service communication
    NUMBER_OF_TYPES
};

/**
 * @brief Class which manages different type of sockets for receiving
 * data. Use epoll for detecting incoming data.
 */
class SocketManager
{
    public:
        /**
         * @brief Constructs a new Socket Manager object
         * 
         * @param if_tracker interface tracker
         * @param neighbor_tracker neighbor tracker
         */
        SocketManager(InterfaceTracker& if_tracker, NeighborTracker& neighbor_tracker);
        
        ~SocketManager();
        // SocketManager(const SocketManager&) = delete;
        SocketManager& operator=(const SocketManager&) = delete;

        /**
         * @brief Adds a socket to epoll to detect incoming messages
         * 
         * @param fd socket file descriptor
         * @param type socket type
         * @return true if socket was added to epoll;
         * @return false if socket was not added to epoll
         */
        bool add_fd_epoll(int fd, SocketType type);
        
        /**
         * @brief Removes a socket from epoll
         * 
         * @param fd socket file descriptor
         * @return true if socket was removed succesfully;
         * @return false if socket removal failed
         */
        bool remove_fd_epoll(int fd);

        /**
         * @brief Tries to get any message on all sockets added to epoll
         * 
         * @param period_ms time in ms how long to scan sockets for msgs
         */
        void poll_msgs(size_t period_ms);

        /**
         * @brief Checks if this class was initialized succesfully
         * 
         * @return true;
         * @return false 
         */
        bool is_ready() const;

        /**
         * @brief Sets socket to be nonblocking
         * 
         * @param fd socket file descriptor
         * @return true if socket was set to nonblocking;
         * @return false if socket failed to set nonblocking
         */
        static bool set_nonblocking_socket(int fd);
    
    private:
        int epoll_fd_ {-1};                                 // epoll file descriptor
        InterfaceTracker& if_tracker_;
        NeighborTracker& neighbor_tracker_;
        std::unordered_map<int, SocketType> fd_to_type_;    // Stores socket type for each socket

        static constexpr int max_epoll_events {MAX_EPOLL_EVENTS}; 
        static constexpr int epoll_timeout_ms {100};
};
