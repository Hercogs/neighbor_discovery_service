#include "interface.hpp"
#include "socket.hpp"
#include "neighbor.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string.h>
#include <vector>

#include <ifaddrs.h>
#include <netdb.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <net/if_arp.h>  // For ARPHRD_ETHER
#include <sys/types.h>
#include <unistd.h>


InterfaceTracker::InterfaceTracker(const std::string& guid)
{
    guid_ = guid;
}

InterfaceTracker::~InterfaceTracker()
{
    fprintf(stdout, "InterfaceTracker: Closing all sockets\n");
    for (auto& [key, value] : if_table_)
    {
        close(value.socket_fd);
    }
}

void InterfaceTracker::add_socket_manager(SocketManager* manager)
{
    socket_manager_ = manager;
}

void InterfaceTracker::add_neighbor_tracker(NeighborTracker* neighbor_tracker)
{
    neighbor_tracker_ = neighbor_tracker;
}

bool InterfaceTracker::scan_interfaces()
{
    remove_inactive_interfaces();       // Remove inactive interfaces

    struct ifaddrs *ifaddr;    
    if (getifaddrs(&ifaddr) == -1)      // Get all network interfaces
    {
        perror("getifaddrs");
        return false;
    }

    // Set all previous active interfaces as inactive.
    for (auto& [key, value] : if_table_)
    {
        value.is_up_and_running = false;
    }

    // Walk throughout linked list to filter interfaces
    for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL) continue;

        // Skip interfaces which are not link layer
        if (ifa->ifa_addr->sa_family != AF_PACKET) continue;

        // Skip not up and non running interfaces
        if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_RUNNING))
            continue;
        
        // Skip non Ethernet interfaces
        struct sockaddr_ll* sll = (struct sockaddr_ll*)ifa->ifa_addr;
        if (sll->sll_hatype != ARPHRD_ETHER)
            continue;
        
        auto it = if_table_.find(ifa->ifa_name);
        if (it != if_table_.end())
        {
            // Interface is already in table
            it->second.is_up_and_running = true;

            // Compare current with past if idx
            if (sll->sll_ifindex != it->second.if_idx)
            {
                // If not equal, then socket should be re-opened
                it->second.if_idx = sll->sll_ifindex; // Save new idx
                
                socket_manager_->remove_fd_epoll(it->second.socket_fd);
                close_interface_socket(it->second.socket_fd);
                it->second.socket_fd = open_interface_socket(it->second);
                if (it->second.socket_fd >= 0)
                {
                    if (!socket_manager_->add_fd_epoll(it->second.socket_fd,
                        it->second.socket_type))
                    {
                        // Adding to epoll failed, close socket and
                        // set interface inactive
                        close_interface_socket(it->second.socket_fd);
                        it->second.is_up_and_running = false;
                    }
                } else
                {
                    it->second.is_up_and_running = false;
                }
            }

            // update IP adresses
            get_ipv4_ipv6(it->second.if_name, it->second.ipv4, it->second.ipv6);

            // Skip all other changes
            continue;
        }

        InterfaceSocketInfo if_info {.is_up_and_running = true};
        if_info.socket_type = SocketType::Interface;

        // save interface name
        memset(if_info.if_name, 0, sizeof(if_info.if_name));
        strncpy(if_info.if_name, ifa->ifa_name, sizeof(if_info.if_name) -1);
        
        if_info.if_idx = sll->sll_ifindex;

        // save raw MAC address
        memset(if_info.mac_raw, 0, sizeof(if_info.mac_raw));
        if (sll->sll_halen > sizeof(if_info.mac_raw))
        {
            fprintf(stderr, "%s interface MAC to long - %d, expected %ld",
                if_info.if_name, sll->sll_halen, sizeof(if_info.mac_raw));
            continue;
        }
        memcpy(if_info.mac_raw, sll->sll_addr, sll->sll_halen);

        // Convert MAC addres to readable string
        std::ostringstream oss;
        for (size_t i = 0; i < sll->sll_halen; ++i)
        {
            if (i > 0) oss << ":";
            oss << std::hex << std::setw(2) << std::setfill('0') << 
            (int)sll->sll_addr[i];
        }
        if_info.mac_name = oss.str();

        // get IP adresses
        get_ipv4_ipv6(if_info.if_name, if_info.ipv4, if_info.ipv6);

        fprintf(stdout, "Adding active %s interface for scanning\n",
            if_info.if_name);

        // Open socket
        if_info.socket_fd = open_interface_socket(if_info);
        if (if_info.socket_fd >= 0)
        {
            if (!socket_manager_->add_fd_epoll(if_info.socket_fd,
                if_info.socket_type))
            {
                // Adding to epoll failed, close socket and
                // set interface inactive
                close_interface_socket(if_info.socket_fd);
                if_info.is_up_and_running = false;
            }
        } else
        {
            if_info.is_up_and_running = false;
        }

        if_table_[std::string(ifa->ifa_name)] = if_info;
    }

    freeifaddrs(ifaddr); // reclaim storage

    return true;
}

void InterfaceTracker::close_interface_socket(int& fd)
{
    if (fd < 0)
    {
        fprintf(stderr, "close_interface_socket(): socket fd invalid");
        return;
    }

    for (size_t i = 0; i < 3; ++i)
    {
        if(close(fd ) != 0)
        {
            perror("Close socket");     // Error on socket close
            if (errno != EINTR)
            {
                break;                  // Asume that socket is closed
            }
        }
    }
    fd = -1;                            // Mark as closed
}

void InterfaceTracker::publish_discovery_msg() const
{
    for (auto& [key, value] : if_table_)
    {
        if (!value.is_up_and_running || !(value.socket_fd >= 0)) 
            continue;
        
        uint8_t frame[1024] {0};
        memset(frame, 0xFF, 6);                 // Header: braodcast address
        memcpy(frame + 6, value.mac_raw, 6);    // Header: mac adress
        // Header: set custom type, later used for identification
        // IPv4 type would be: 0x08, 0x00
        frame[12] = static_cast<uint8_t>(InterfaceTracker::protocol_id >> 8);
        frame[13] = static_cast<uint8_t>(InterfaceTracker::protocol_id & 0xff);

        // Payload
        DiscoveryMessage msg {0};
        strncpy(msg.guid, guid_.data(), sizeof(msg.guid) - 1);
        strncpy(msg.if_name, value.if_name, sizeof(msg.if_name) - 1);
        strncpy(msg.mac_name, value.mac_name.data(), sizeof(msg.mac_name) - 1);
        strncpy(msg.ipv4, value.ipv4.data(), sizeof(msg.ipv4) - 1);
        strncpy(msg.ipv6, value.ipv6.data(), sizeof(msg.ipv6) - 1);
        msg.update_checksum();
        
        // Check payload size
        if (sizeof(frame) < (sizeof(msg) + InterfaceTracker::ethernet_header_size))
        {
            fprintf(stderr, "Sending discovery msg failed. Buffer too small.\n");
            return;
        }
        memcpy(frame + InterfaceTracker::ethernet_header_size, &msg, sizeof(msg));

        struct sockaddr_ll sll {0};
        sll.sll_family      = AF_PACKET;
        sll.sll_ifindex     = value.if_idx;
        sll.sll_halen       = ETH_ALEN;
        sll.sll_protocol    = htons(InterfaceTracker::protocol_id);    // Set as IPv4 protocol 0x0800
        memset(sll.sll_addr, 0xFF, 6);

        // Non blocking socket, do not care about readiness
        ssize_t bytes_sent = sendto(value.socket_fd, frame,
            InterfaceTracker::ethernet_header_size + sizeof(msg), 0,
            (struct sockaddr*) &sll, sizeof(sll));
        
        if (bytes_sent == -1)
        {
            perror("sendto discovery msg");
        }
        // fprintf(stdout, "For interface %s sent %ld bytes\n",
        //     value.if_name, bytes_sent);
    }              
}

void InterfaceTracker::handle_socket_msg(int fd)
{
    struct sockaddr_ll sender {0};
    socklen_t sender_size = sizeof(sender);
    memset(buffer_, 0, sizeof(buffer_));

    ssize_t bytes_received {0};
    bytes_received = recvfrom(fd, buffer_, sizeof(buffer_), 0,
                    (sockaddr*)&sender, &sender_size);
    
    if (bytes_received < 0)
    {
        perror("recvfrom");
        if (errno == EBADF || errno == ENETDOWN || errno == ENODEV)
        {
            // TODO: process error in future if needed
        }
        return;
    }

    // Decode msg
    if (bytes_received == (sizeof(struct DiscoveryMessage) +
        InterfaceTracker::ethernet_header_size))
    {
        if ((buffer_[12] == static_cast<uint8_t>(InterfaceTracker::protocol_id >> 8)) &&
            (buffer_[13] == static_cast<uint8_t>(InterfaceTracker::protocol_id & 0xff)))
        {   
            DiscoveryMessage msg;
            memcpy(&msg, buffer_ + InterfaceTracker::ethernet_header_size,
                sizeof(msg));

            if (msg.verify_checksum())
            {
                // Store it in neighbor tracker, if not from self host
                if (msg.guid != guid_)
                {
                    neighbor_tracker_->add_neighbor(msg);
                }
            }
        }
    }
}

void InterfaceTracker::print_if_table() const
{
    for (const auto& [key, value] : if_table_)
    {
        fprintf(stdout, "%-10s if_idx: %-3d \t ipv4: %s \t ipv6: %s",
            value.if_name, value.if_idx,
            value.ipv4.empty() ? "None" : value.ipv4.c_str(),
            value.ipv6.empty() ? "None" : value.ipv6.c_str());
        fprintf(stdout, "\t mac: %s", value.mac_name.c_str());
        fprintf(stdout, "\tUP/RUNNING: %s\n", (value.is_up_and_running)? "true" : "false");
    }
}

int InterfaceTracker::open_interface_socket(const InterfaceSocketInfo& if_socket)
{
    int socket_fd = if_socket.socket_fd;
    if (socket_fd >= 0) close_interface_socket(socket_fd);

    // fprintf(stdout, "Open socket for %s interface\n", if_socket.if_name);
    socket_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (socket_fd  < 0)
    {
        perror("Socket");
        return -1;
    }

    if (!SocketManager::set_nonblocking_socket(socket_fd))
    {
        close(socket_fd);
        return -1;
    }

    // fprintf(stdout, "Bind socket for %s interface\n", if_socket.if_name);
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sockaddr_ll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = if_socket.if_idx;
    sll.sll_protocol = htons(ETH_P_ALL);
    if (bind(socket_fd, (struct sockaddr*)&sll, sizeof(sll)) < 0)
    {
        perror("bind socket");
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}

void InterfaceTracker::remove_inactive_interfaces()
{
    auto it = if_table_.begin();
    while(it != if_table_.end())
    {
        if (!it->second.is_up_and_running)
        {
            fprintf(stdout, "Removing inactive %s interface\n", it->second.if_name);
            socket_manager_->remove_fd_epoll(it->second.socket_fd);
            close(it->second.socket_fd);
            it = if_table_.erase(it);
        } else
        {
            ++it;
        }
    }
}

void InterfaceTracker::get_ipv4_ipv6(char if_name[IFNAMSIZ],
    std::string& ipv4, std::string& ipv6)
{
    ipv4 = "None";
    ipv6 = "None";

    char host[NI_MAXHOST] {0};
    struct ifaddrs *ifaddr {0};

    // Get all network interfaces
    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        return;
    }

    // Wlak throughout linked list to filter interfaces
    for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL) continue;

        // Search for specific name
        if (strncmp(if_name, ifa->ifa_name, IFNAMSIZ) != 0)
        {
            continue;
        }
        int s, family = ifa->ifa_addr->sa_family;
        if (family == AF_INET || family == AF_INET6)
        {
            s = getnameinfo(ifa->ifa_addr,
                (family == AF_INET) ? sizeof(struct sockaddr_in) : 
                sizeof(struct sockaddr_in6), host, NI_MAXHOST, NULL, 0,
                NI_NUMERICHOST      // get IP adress 
            );
            
            if (s != 0)
            {
                fprintf(stderr, "getnameinfo() failed");
                continue;
            }

            if (family == AF_INET) ipv4 = host;
            if (family == AF_INET6) ipv6 = host;
            
            // Remove interface name for ipv6
            size_t percent = ipv6.find('%');
            if (percent != std::string::npos)
            {
                ipv6 = ipv6.substr(0, percent);
            }
        } 
    }

    freeifaddrs(ifaddr); // reclaim storage
    return;  
}

