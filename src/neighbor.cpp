#include "common.hpp"
#include "interface.hpp"
#include "neighbor.hpp"
#include "socket.hpp"
#include "utils.hpp"

#include <iostream>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

NeighborTracker::NeighborTracker()
{
    // Uncomment to generate 30 000 neighbors for test purpose at the beginin.
    // Remember that after 30 seconds they will be deleted.
    // generate_pseudo_data(30000, neighbor_table_);
}

NeighborTracker::~NeighborTracker()
{
    close(unix_server_socket_);     // Close server socket
    close(unix_data_socket_);       // Close data socket
    unlink(unix_socket_path);       // Dletes socket path
    if (output_buffer_) free(output_buffer_);
}

void NeighborTracker::add_neighbor(const DiscoveryMessage& msg)
{
    ConnectionInfo ci {
        .timestamp = get_current_time_ms()
    };
    memcpy(ci.mac_name, msg.mac_name, sizeof(ci.mac_name));
    memcpy(ci.ipv4, msg.ipv4, sizeof(ci.ipv4));
    memcpy(ci.ipv6, msg.ipv6, sizeof(ci.ipv6));

    neighbor_table_[msg.guid][msg.if_name] = ci;
}

void NeighborTracker::remove_inactive_neighbors()
{
    uint64_t current_time = get_current_time_ms();

    auto it = neighbor_table_.begin();

    // Iterate over every GUDI
    while(it != neighbor_table_.end())
    {
        // Iterate over every interface for each GUID
        auto it_inner = it->second.begin();
        while(it_inner != it->second.end())
        {
            if ((current_time - it_inner->second.timestamp) > NeighborTracker::inactivity_timeout_ms)
            {
                it_inner = it->second.erase(it_inner);  // Remove inactive interface
            } else{
                ++it_inner;
            }
        }

        if (it->second.begin() == it->second.end())
        {
            it = neighbor_table_.erase(it);             // Remove GUID, no active interfaces
        } else{
            ++it;
        }
    }
}

void NeighborTracker::print_neighbours() const
{
    size_t neighbors {0}, interfaces {0};
    neighbors = neighbor_table_.size();

    fprintf(stdout, "Active neighbor list:\n\n");
    for (const auto& [key, value] : neighbor_table_)
    {
        interfaces += value.size();
        fprintf(stdout, "ID: %s\n", key.c_str());
        fprintf(stdout, "  Interfaces:\n");
        for (const auto& [key1, value1] : value)
        {
            fprintf(stdout, "    - Name     : %s\n", key1.c_str());
            fprintf(stdout, "      MAC      : %s\n", value1.mac_name);
            fprintf(stdout, "      IPv4     : %s\n", value1.ipv4);
            fprintf(stdout, "      IPv6     : %s\n", value1.ipv6);
            fprintf(stdout, "\n");
        }
    }
    fprintf(stdout, "Summary: %ld neighbour(s), %ld interface(s).\n\n",
        neighbors, interfaces);
}

void NeighborTracker::add_socket_manager(SocketManager* manager)
{
    socket_manager_ = manager;
}

bool NeighborTracker::is_ready() const
{
    return (unix_server_socket_ >= 0);
}

void NeighborTracker::setup_unix_server_socket()
{
    unix_server_socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (unix_server_socket_  < 0)
    {
        perror("Unix socket");
        return;
    }

    if (!SocketManager::set_nonblocking_socket(unix_server_socket_))
    {
        close(unix_server_socket_);
        unix_server_socket_ = -1;
        return;
    }

    sockaddr_un addr {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, unix_socket_path, sizeof(addr.sun_path)-1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = 0;

    unlink(addr.sun_path);                      // Delete socket path file
    if (bind(unix_server_socket_, (sockaddr*)&addr, sizeof(addr)) == -1)
    {
        perror("bind unix socket");
        close(unix_server_socket_);
        unix_server_socket_ = -1;
        return;
    }

    if (listen(unix_server_socket_, 1) == -1)   // Max 1 client in waiting
    {
        perror("listen");
        close(unix_server_socket_);
        unix_server_socket_ = -1;
        return;
    }

    if (!socket_manager_->add_fd_epoll(unix_server_socket_,
                        SocketType::Unix))
    {
        close(unix_server_socket_);
        unix_server_socket_ = -1;
    }
}

void NeighborTracker::handle_socket_msg(int fd)
{
    if (fd == unix_server_socket_)
    {
        // Unix server socket
        struct sockaddr_un sender;
        socklen_t sender_size = sizeof(sender);
        int socket_id = accept(fd, (struct sockaddr*)&sender, &sender_size);
        if (socket_id <= 0)
        {
            close(socket_id);
            perror("accept");
            return;
        }

        if (unix_data_socket_ >= 0)
        {
            // Allow only 1 data socket at time
            fprintf(stderr, "New unix socket (%d) rejected: %d already active.\n",
                socket_id, unix_data_socket_);
            close(socket_id);
            return;
        }

        unix_data_socket_ = socket_id;
        setup_unix_data_socket();
    } else if(fd == unix_data_socket_)
    {
        // Unix data socket
        struct sockaddr_un sender;
        socklen_t sender_size = sizeof(sender);
        uint8_t buffer[1024] {0};

        int64_t bytes_received = recvfrom(unix_data_socket_, buffer,
            sizeof(buffer), 0, (sockaddr*)&sender, &sender_size);
        
        // fprintf(stdout, "Number of bytes on unix socket: %ld\n", bytes_received);
        
        int64_t buffer_size {0};
        UnixSocketRequest req;
        if (bytes_received == sizeof(req))
        {
            //  If valid data, create output buffer
            memcpy(&req, buffer, sizeof(req));
            buffer_size = write_output_buffer(req.request_only_neighbor_cnt);
        }
        // fprintf(stdout, "Buffer size %ld\n", buffer_size);

        ssize_t total_sent {0};
        if (buffer_size >= 0)
        {
            if (send(unix_data_socket_, &buffer_size, sizeof(int64_t), MSG_NOSIGNAL)
                    == sizeof(int64_t))  // Send buffer size, do not raise signal 
            {
                // fprintf(stdout, "Sent msg header: %ld bytes\n", sizeof(int64_t));
                while (total_sent < buffer_size)
                {
                    ssize_t bytes_sent = send(unix_data_socket_,
                        output_buffer_ + total_sent, buffer_size - total_sent, 0);
                    if (bytes_sent <= 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            // Add socket to poll to contnue sending when ready
                            struct pollfd pollfd {
                                .fd = unix_data_socket_,
                                .events = POLLOUT,
                                .revents = 0
                            };
                            int poll_rc = poll(&pollfd, 1, 100);    // wait max 100ms
                            if (poll_rc == -1) 
                            {
                                perror("poll");
                                break;
                            } else if (poll_rc == 0)
                            {
                                fprintf(stderr, "Unix data socket not sent full msg\n");
                                break;
                            } else
                            {
                                continue;
                            }
                        } else
                        {
                            perror("sent unix data socket");
                            break;
                        }
                    }
                    total_sent += bytes_sent;
                }
            }
        }
        // fprintf(stdout, "Sent msg body: %ld bytes\n", total_sent);

        if (output_buffer_)
        {
            free(output_buffer_);       // deallocate
            output_buffer_ = nullptr;
        }

        // clean up unix data socket
        socket_manager_->remove_fd_epoll(unix_data_socket_);
        close(unix_data_socket_);
        unix_data_socket_ = -1;
    }
}

void NeighborTracker::setup_unix_data_socket()
{

    if (!SocketManager::set_nonblocking_socket(unix_data_socket_))
    {
        close(unix_data_socket_);
        unix_data_socket_ = -1;
        return;
    }

    if (!socket_manager_->add_fd_epoll(unix_data_socket_,
                        SocketType::Unix))
    {
        close(unix_data_socket_);
        unix_data_socket_ = -1;
    }
}

ssize_t NeighborTracker::write_output_buffer(bool only_neighbor_cnt)
{
    if (only_neighbor_cnt)
    {
        output_buffer_ = (char*)malloc(sizeof(int64_t));
        if (output_buffer_ == nullptr) return -1;

        int64_t cnt = static_cast<int64_t>(neighbor_table_.size());
        memcpy(output_buffer_, &cnt, sizeof(int64_t));
        return sizeof(int64_t);
    }
    
    // Write full list of neighbors
    size_t initial_buffer_size {
        neighbor_table_.size() * sizeof(struct ConnectionInfo)};
    output_buffer_ = (char*)malloc(initial_buffer_size);
    if (output_buffer_ == nullptr) return -1;
    size_t capacity = initial_buffer_size;
    size_t length {0};

    // Lambda for checking capacity
    auto check_capacity = [&](size_t needed_bytes) -> bool
    {
        size_t required = length + needed_bytes;
        if (required > capacity)
        {
            size_t new_capacity = std::max(required, capacity * 2);
            char* new_buffer = (char*)realloc(output_buffer_, new_capacity);
            if (new_buffer == nullptr)
            {
                perror("realloc");
                return false;
            }
            output_buffer_ = new_buffer;
            capacity = required;
        }
        return true;
    };

    // lambda for inserting data
    auto insert_data = [&](const void* data, size_t len) -> bool
    {
        // fprintf(stdout, "Inserting %ld data at %ld udx,\n", len, length);
        if (!check_capacity(len))
        {
            return false;
        }
        memcpy(output_buffer_ + length, data, len);
        length += len;
        return true;
    };

    /*
    * Buffer structure:
    * [
    *   number_of_neighbors                 uint32_t
    *       neighbor_name_size              uint32_t
    *       neighbor_name
    *           number_of_interfaces        uint32_t
    *               interface_name_size     uint32_t
    *               interface_name
    *               struct ConnectionInfo
    *       ...
    * ]
    */

    uint32_t number_of_neighbors = neighbor_table_.size();
    // Save number of neighbors
    if (!insert_data(&number_of_neighbors, sizeof(number_of_neighbors)))
    {
        return -1;
    }

    // Iterate over each neighbor
    for (auto const& [key, value] : neighbor_table_)
    {
        // Save neighbor name size and name
        uint32_t  neighbor_id_size = key.size();
        if (!insert_data(&neighbor_id_size, sizeof(neighbor_id_size)) ||
            !insert_data(key.data(), neighbor_id_size))
        {
            return -1;
        }
        // Save number of interfaces
        uint32_t number_of_interfaces = value.size();
        if (!insert_data(&number_of_interfaces, sizeof(number_of_interfaces)))
        {
            return -1;
        }
        // Iterate over each interface for neighbor
        for (auto const& [key_in, value_in] : value)
        {
            // Save interface name
            uint32_t  interface_name_size = key_in.size();
            if (!insert_data(&interface_name_size, sizeof(interface_name_size)) ||
                !insert_data(key_in.data(), interface_name_size))
            {
                return -1;
            }
            // Save interface info
            if (!insert_data(&value_in, sizeof(value_in)))
            {
                return -1;
            }
        }
    }

    return length;
}
