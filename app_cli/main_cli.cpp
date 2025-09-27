#include "common.hpp"
#include "socket.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits.h>
#include <poll.h>
#include <sstream>
#include <string>


/**
 * @brief Print argument parser error/helper message
 * 
 * @param exe_name executable name
 */
void print_parse_arg_err_msg(const char* exe_name)
{
    fprintf(stdout, "Usage: %s [-n] [-f filename]\n\n"
        "Options:\n"
        "   -n          Get only active neigbor count (optional)\n"
        "               If -n not used, detailed information is retuned\n"
        "   -f          Output filename (obligatory if -n is not used)\n",
        exe_name);
}

/**
 * @brief Argument parser function
 * 
 * @param argc argument count
 * @param argv arguments
 * @param request_only_neighbor_cnt flag storage for argument -n
 * @param filename filename storage for argument -f
 */
void parse_args(const int argc, char** argv,
    bool* request_only_neighbor_cnt, char filename[NAME_MAX])
{
    int opt;
    while ((opt = getopt(argc, argv, "nf:h")) != -1)
    {
        switch (opt)
        {
            case 'n':
                *request_only_neighbor_cnt = true;
                break;
            case 'f':
                if (strlen(optarg) >= NAME_MAX - 1)
                {
                    fprintf(stderr, "Passed filename too long\n");
                    exit(EXIT_FAILURE);
                }
                strncpy(filename, optarg, NAME_MAX);
                break;
            case 'h':
                print_parse_arg_err_msg(argv[0]);
                exit(EXIT_SUCCESS);
            case '?':
                print_parse_arg_err_msg(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                print_parse_arg_err_msg(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (optind < argc)
    {
        print_parse_arg_err_msg(argv[0]);
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Creates a unix client socket object
 * 
 * @return int socket file descriptor
 */
int create_unix_client_socket()
{   
    int client_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_sock < 0) {
        perror("Client socket");
        return -1;
    }
    int rc = SocketManager::set_nonblocking_socket(client_sock);
    if (!rc)
    {
        close(client_sock);
        return -1;
    }
    return client_sock;
}

/**
 * @brief Function to connect cleint socket to server socket
 * 
 * @param socket_fd socket file descriptor
 * @return true if socket connected;
 * @return false if socket no connected
 */
bool connect_socket(int socket_fd)
{
    // Set up address
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, unix_socket_path, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = 0;

    int rc = connect(socket_fd, (sockaddr*)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS)
    {
        return false;
    }

    struct pollfd pollfd {
        .fd = socket_fd,
        .events = POLLOUT,
        .revents = 0
    };
    int poll_rc = poll(&pollfd, 1, 100);    // wait max 100ms
    if (poll_rc == -1) 
    {
        perror("poll");
        return false;                       // error
    } else if (poll_rc == 0)
    {
        fprintf(stdout, "Timeout\n");
        return false;                       // Timed out
    }
    // Check socket error storage
    int err {0};
    socklen_t len = sizeof(err);
    if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
    {
        perror("getsocketopt");             // Failed to get error
        return false;
    } else if (err == 0)
    {
        return true;                        // OK
    } else
    {
        return false;                       // Connection failed
    }
    return false;
}

/**
 * @brief Function to read portion of incoming message
 * 
 * @param socket_fd socket file descriptor
 * @param buffer data buffer storage
 * @param buffer_size how many bytes to read
 * @return true 
 */
bool read_incoming_msg_size(int socket_fd, void* buffer, int64_t buffer_size)
{
    struct pollfd pollfd {
        .fd = socket_fd,
        .events = POLLIN,
        .revents = 0
    };

    int total_received {0};
    char* buffer_ptr = static_cast<char*>(buffer);

    while (total_received < buffer_size)
    {
        int poll_rc = poll(&pollfd, 1, 500);    // wait max 500ms
        if (poll_rc <= 0) return false;         // Timeout or error
        ssize_t bytes_recv = recv(socket_fd, buffer_ptr + total_received,
            buffer_size - total_received, 0);
        if (bytes_recv <= 0) return false;
        total_received += bytes_recv;
    }
    return true;
}

/**
 * @brief Function to parse full neighbor list message
 * 
 * @param oss output string stream pointer to store data
 * @param buffer data buffer
 * @param buffer_size data buffer size
 * @return true if parsing is succesful;
 * @return false if parsing was not succesful
 */
bool parse_full_neighbor_list(std::ostringstream* oss, const char* buffer,
    uint64_t buffer_size)
{
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

    uint64_t bytes_readed {0};

    // lambda for reading data chunks
    auto read_data = [&](void* data, const size_t len) -> bool
    {
        if (bytes_readed + len > buffer_size)
        {
            // Not enough data in buffer
            return false;
        }
        memcpy(data, buffer + bytes_readed, len);
        bytes_readed += len;
        return true;
    };

    // storage structure
    std::unordered_map<
            std::string,
            std::unordered_map<
                std::string,
                ConnectionInfo>> neighbor_table;

    uint32_t number_of_neighbors;
    if (!read_data(&number_of_neighbors, sizeof(number_of_neighbors)))
        return false;
    
    for (size_t i = 0; i < number_of_neighbors; ++i)
    {
        uint32_t neighbor_name_size {0};
        if (!read_data(&neighbor_name_size, sizeof(neighbor_name_size)))
            return false;
        
        std::string neighbor_name(neighbor_name_size, '\0');
        if (!read_data(&neighbor_name[0], neighbor_name_size))
            return false;
        
        uint32_t number_of_interfaces {0};
        if (!read_data(&number_of_interfaces, sizeof(number_of_interfaces)))
            return false;
        
        for (size_t j = 0; j < number_of_interfaces; ++j)
        {
            uint32_t interface_name_size {0};
            if (!read_data(&interface_name_size, sizeof(interface_name_size)))
                return false;
            
            std::string interface_name(interface_name_size, '\0');
            if (!read_data(&interface_name[0], interface_name_size))
                return false;
            
            ConnectionInfo connection_info {0};
            if (!read_data(&connection_info, sizeof(connection_info)))
                return false;
            
            // save entry in table
            neighbor_table[neighbor_name][interface_name] = connection_info;
        }
    }
    // Check if all bytes were used and strucure filled
    if (bytes_readed != buffer_size) return false;

    // Store in string stream
    int interfaces {0};
    *oss << "Active neighbor list:\n\n";
    for (const auto& [key, value] : neighbor_table)
    {
        interfaces += value.size();
        *oss << "ID: " <<  key << '\n';
        *oss << "  Interfaces:\n"; 
        for (const auto& [key1, value1] : value)
        {
            *oss << "    - Name     : " << key1 << '\n';
            *oss << "      MAC      : " << value1.mac_name << '\n';
            *oss << "      IPv4     : " << value1.ipv4 << '\n';
            *oss << "      IPv6     : " << value1.ipv6 << '\n';
            *oss << '\n';
        }
    }
    *oss << "Summary: " << number_of_neighbors << " neighbour(s), " <<
         interfaces << " interface(s).\n\n";
    return true;
}

int main(int argc, char** argv)
{

    bool request_only_neighbor_cnt  {false};        // Output only number of neighbors flag
    char out_filename[NAME_MAX]     {0};            // Filename for result storage    
    parse_args(argc, argv, &request_only_neighbor_cnt, out_filename);

    // Check passed options
    if (!request_only_neighbor_cnt && (strlen(out_filename) == 0))
    {
        fprintf(stderr, "Error: No filename passed.\n");
        print_parse_arg_err_msg(argv[0]);
        return -1;
    }

    // Create socket and set non blocking
    int socket_fd = create_unix_client_socket();
    if (socket_fd < 0)
    {
        fprintf(stderr, "Error creating socket.\n");
        return -1;
    }

    // Connect to server socket
    if (!connect_socket(socket_fd))
    {
        // Connection failed
        fprintf(stderr, "Connection to background server failed."
            " Check if it is running\n");
        return -1;
    }
    
    // Send msg
    UnixSocketRequest req
    {
        .request_only_neighbor_cnt = request_only_neighbor_cnt
    };
    if (send(socket_fd, &req, sizeof(req), 0) != sizeof(req)) {
        perror("Send");
        fprintf(stderr, "Connection to background server interrupted. Try again\n");
        return -1;
    }


    // Read incoming msg size
    ssize_t msg_size;
    if (!read_incoming_msg_size(socket_fd, &msg_size, sizeof(msg_size)))
    {
        fprintf(stderr, "Connection to background server interrupted. Try again\n");
        return -1;
    }

    // Create buffer for incoming msg
    char* input_buffer = (char*)malloc(msg_size);
    if (!input_buffer)
    {
        perror("malloc");
        return -1;
    }

    // Read msg
    if (!read_incoming_msg_size(socket_fd, input_buffer, msg_size))
    {
        free(input_buffer);
        fprintf(stderr, "Connection to background server interrupted. Try again\n");
        return -1;
    }

    std::ostringstream oss;

    if (request_only_neighbor_cnt)  // Only number of neighbors were requested
    {
        int64_t number_of_neighbors;
        memcpy(&number_of_neighbors, input_buffer,
            sizeof(number_of_neighbors));
        oss << "Summary: This host has " << number_of_neighbors <<
            " active neighbor(s)" << std::endl;
    } else                          // Full neighbor list were asked
    {
        if (!parse_full_neighbor_list(&oss, input_buffer, msg_size))
        {
            free(input_buffer);
            fprintf(stderr, "Error: data corrupted\n");
            return -1;
        }
    }
    if (input_buffer) free(input_buffer);

    // Create output
    if (strlen(out_filename) > 0)
    {
        // Save to file
        std::ofstream ofs(out_filename);
        if (!ofs)
        {
            fprintf (stderr, "Failed to open file: %s\n", out_filename);
        }
        ofs << oss.str();
        ofs.close();
        fprintf (stderr, "Output saved to file: %s\n", out_filename);
    } else
    {
        // print in terminal
        fprintf(stdout, "%s", oss.str().c_str());
    }
}