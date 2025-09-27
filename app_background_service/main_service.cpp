#include "interface.hpp"
#include "neighbor.hpp"
#include "socket.hpp"
#include "utils.hpp"

#include <sys/file.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

/**
 * @brief Wrapper class to launch background service cleaner
 */
class ServiceManager
{
    public:
        // Helper function to create Service Manager, because socket_manager_ uses references
        static ServiceManager create()
        {
            fprintf(stdout, "Background service started!" 
                " Ctrl+C to stop!\n");
            std::string guid = generate_guid();
            fprintf(stdout, "GUID: %s\n", guid.c_str());
            
            return ServiceManager(guid);
        }

        /**
         * @brief Checks if all need objects are initlized corectly
         * 
         * @return true if instance is ready to be used;
         * @return false if instance is not ready to be used
         */
        bool is_ready() const {return is_ready_;}

        /**
         * @brief main loop of background service
         * 
         */
        void run()
        {
            while (true)
            {
                if (shutdown_request_)
                    {
                        break;
                    }
                
                if_tracker_.scan_interfaces();              // Get active interface
                // if_tracker.print_if_table();

                if_tracker_.publish_discovery_msg();        // Publish discovery msg
                socket_manager_.poll_msgs(1000);            // Poll msgs for 1000ms
                
                neighbor_tracker_.remove_inactive_neighbors();  // Remove inactive neighbors

                // neighbor_tracker_.print_neighbours();    // print neighbors
            }
        }

    private:
        /**
         * @brief Constructs a new Service Manager object
         * 
         * @param guid GUID for this host
         */
        ServiceManager(const std::string& guid) : if_tracker_(guid), 
                neighbor_tracker_(), socket_manager_(if_tracker_, neighbor_tracker_)
        {
            std::signal(SIGINT, &ServiceManager::handle_sigint);
            std::signal(SIGTERM, &ServiceManager::handle_sigint);
            
            // Configure submodules
            if_tracker_.add_socket_manager(&socket_manager_);
            if_tracker_.add_neighbor_tracker(&neighbor_tracker_);
            neighbor_tracker_.add_socket_manager(&socket_manager_);
            neighbor_tracker_.setup_unix_server_socket();

            // Checks if submdules are initialized
            is_ready_ = socket_manager_.is_ready() && 
                neighbor_tracker_.is_ready();
        }

        /**
         * @brief signal handler for clean exit. Set shutdown flag.
         * 
         * @param signal signal
         */
        static void handle_sigint(int signal)
        {
            shutdown_request_ = true;
        }

        bool is_ready_ {false};                 // flag for module readiness
        static volatile std::sig_atomic_t shutdown_request_;    // flag for shutdown request
        InterfaceTracker if_tracker_;           // interfce tracker
        NeighborTracker neighbor_tracker_;      // neighbor tracker
        SocketManager socket_manager_;          // socket manager
};

volatile std::sig_atomic_t ServiceManager::shutdown_request_ {0};


int main(int argc, char** argv)
{
    // add lock file for allowing  single instance only
    const char lfile[] = "/tmp/neighbor_service.lock";
    int fd = open(lfile, O_CREAT | O_RDWR, 0666);
    if (fd < 0)
    {
        perror("open");
        fprintf(stderr, "Failed to open lock file\n");
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) < 0)
    {
        perror("flock");
        fprintf(stderr, "Another background service is already running\n");
        return -1;
    }

    ServiceManager service = ServiceManager::create();
    if (service.is_ready())
    {
        service.run();
    } else 
    {
        fprintf(stderr, "Background service failed to start!\n");
    }

    return 0;
}