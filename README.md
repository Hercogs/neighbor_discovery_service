# Neighbor Discovery Service
Background service that discovers and maintains a list of neighboring devices on Ethernet networks, where neighbors are defined as other devices running the same service. Additionally, there is a CLI program to query and display the neighbor list. Supports Ubuntu 24.04 LTS. Full details are found at the end of repo.

## Dependencies
- `make`
- `g++`

Ensure these are installed before building the project.
```bash
sudo apt update && apt install make g++
```

## Installation
Clone the repository and build the project:
```bash
git clone https://github.com/Hercogs/neighbor_discovery_service.git
cd neighbor_discovery_service
make
```

## Running the program
To start neighbor discovery service, execute:
```bash
sudo ./build/app_background_service
```
To run CLI program, execute one of the option:
```bash
sudo ./build/app_cli -n                 # gets number of active neighbors printed in console
sudo ./build/app_cli -f out.txt         # gets full list of active neighbors and saved in out.txt file
sudo ./build/app_cli -n -f out.txt      # gets number of active neighbors saved in out.txt file
```

## Experiment
Experiment was done in `libvirt/virt-manager` based VM network. 4 virtual machines and 3 networks were used. Pseudo structure for connections were like:
- VM1 -> Network1, Network2, Network3
- VM2 -> Network1, Network2, Network3
- VM3 -> Network1, Network2
- VM4 -> Network1

Output files for active neighbors for VM1 and VM4 can be found here:
- [View VM1 output](./experiment/VM1_output.txt)
- [View VM4 output](./experiment/VM4_output.txt)

## Repository structure
```t
neighbor_discovery_service/
├── app_background_service      # entrypoint for background service
├── app_cli                     # entrypoint for CLI program
├── experiment                  # experiment/test output files
├── include
├── Makefile            
├── README.md
├── src
├── valgrind_test_cli.bash      # valgrind test file for CLI
└── valgrind_test_service.bash  # valgrind test file for background service
```

## Full project overview
##### Overview
Develop a background service that discovers and maintains a list of neighboring devices on Ethernet networks, where neighbors are defined as other devices running the same service. Additionally, implement a CLI program to query and display the neighbor list. Note that two devices may be connected through multiple networks simultaneously.

##### Background Service Requirements

- Write a background service that discovers neighbors running the same service in connected Ethernet networks
- Service must handle multiple Ethernet interfaces on the device running the service
- Service must adapt to interfaces that appear, disappear, or change state dynamically
- Service must run on all active ("running") Ethernet interfaces, including those without IP addresses (IPv4 or IPv6)
- Neighbors are defined as pairs of devices running the service connected by at least one Ethernet network
- An active connection is an Ethernet interface where the neighbor was reachable within the last 30 seconds
- Service maintains a list of active neighbors (neighbors with at least one active connection)

##### CLI Program Requirements

- Write a CLI program that retrieves neighbor data from the background service running on the current device
- For each active neighbor of the current device, display all active connections
- For each connection, display the local interface name, neighbor's MAC address in that specific network, and neighbor's IP address in that network (or indicate if none exists)

##### Technical Constraints

- Must use g++ compiler with standard C++ library (libstdc++)
- Must use make for building
- Cannot use threads in the implementation
- Cannot use exceptions in the implementation
- Service and CLI must run on current Ubuntu LTS version
- Service is launched manually (no automatic startup required)
- Cannot use or execute other system tools/programs
- Must handle at least 10,000 active neighbors efficiently

##### Testing Requirements
- Recommended to test using libvirt/virt-manager based VM network
- Test environment should have at least 3 interconnected virtual machines
- At least 2 VMs must be connected by multiple separate networks to test multi-link scenarios

