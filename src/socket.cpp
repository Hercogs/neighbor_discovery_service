#include "interface.hpp"
#include "neighbor.hpp"
#include "socket.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netpacket/packet.h> // sockaddr_ll
#include <net/ethernet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>


SocketManager::SocketManager(InterfaceTracker& if_tracker,
            NeighborTracker& neighbor_tracker) :
    if_tracker_(if_tracker), neighbor_tracker_(neighbor_tracker)
{
    epoll_fd_ = epoll_create1(0);
    if ( epoll_fd_ == -1)
    {
        perror("epoll_create1");
    }
}

SocketManager::~SocketManager() {}

bool SocketManager::add_fd_epoll(int fd, SocketType type)
{
    struct epoll_event ev {0};
    ev.events = EPOLLIN;            // | EPOLLOUT
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        perror("epoll_ctl() add socket");
        return false;
    }
    fd_to_type_[fd] = type;         // Stores socket type
    return true;
}

bool SocketManager::remove_fd_epoll(int fd)
{
    struct epoll_event ev {0};
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &ev) == -1)
    {
        perror("epoll_ctl() remove socket");
        return false;
    }
    fd_to_type_.erase(fd);          // Remove socket type
    return true;
}

void SocketManager::poll_msgs(size_t period_ms)
{
    int nfds {0};
    struct epoll_event events[SocketManager::max_epoll_events];

    uint64_t now, elapsed, start_time = get_current_time_ms();
    do
    {
        nfds = epoll_wait(epoll_fd_, events, SocketManager::max_epoll_events,
            SocketManager::epoll_timeout_ms);
        if (nfds == -1)
        {
            perror("epoll_wait");
        }
        for (int i = 0; i < nfds; ++i)
        {
            if (events[i].events & EPOLLIN)
            {
                int fd = events[i].data.fd;
                auto it = fd_to_type_.find(fd);
                if (it == fd_to_type_.end())
                {
                    fprintf(stderr, "Unknown socket in epoll: %d\n",
                        fd);
                    continue;
                }
                SocketType type = it->second;
                if (type >= SocketType::NUMBER_OF_TYPES)
                {
                    fprintf(stderr, "Unsuported socket type: %d\n", (int)type);
                    continue;
                }
                if (type == SocketType::Interface)
                {
                    if_tracker_.handle_socket_msg(fd);          // handle msg
                }
                if (type == SocketType::Unix)
                {
                    neighbor_tracker_.handle_socket_msg(fd);    // handle msg
                }
            }
        }
        now = get_current_time_ms();
        elapsed = now - start_time;
    } while (elapsed < period_ms);
}

bool SocketManager::is_ready() const
{
    return (epoll_fd_ >= 0);
}

bool SocketManager::set_nonblocking_socket(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        perror("fcntl() get flags");
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("fcntl() set flags");
        return false;
    }
    return true;
}
