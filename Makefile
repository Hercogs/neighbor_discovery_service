
CXX = g++
CXXFLAGS = --std=c++17
CPPFLAGS = -g -Wall -DEBUG -MMD -MP

INC = -I./include

SRC_DIR = src
BUILD_DIR = build
SERVICE_DIR = app_background_service
CLI_DIR = app_cli

SRCS := $(wildcard src/*.cpp)
OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

SERVICE_MAIN = $(SERVICE_DIR)/main_service.cpp
SERVICE_OBJS = main_service.o

CLI_MAIN = $(CLI_DIR)/main_cli.cpp
CLI_OBJS = main_cli.o

all: $(BUILD_DIR)/app_background_service $(BUILD_DIR)/app_cli
.PHONY: all

$(BUILD_DIR)/app_background_service: $(OBJS) $(BUILD_DIR)/$(SERVICE_OBJS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INC) -o $@ $^

$(BUILD_DIR)/app_cli: $(OBJS) $(BUILD_DIR)/$(CLI_OBJS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INC) -o $@ $^

# cpp to bject for src folder
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INC) -c $< -o $@
# -c $< ---> compile each cpp file into object file -o $@

# cpp to object file for app_background_service folder
$(BUILD_DIR)/$(SERVICE_OBJS): $(SERVICE_MAIN)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INC) -c $< -o $@

# cpp to object file for app_cli folder
$(BUILD_DIR)/$(CLI_OBJS): $(CLI_MAIN)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INC) -c $< -o $@

-include $(OBJS:.o=.d)
-include $(BUILD_DIR)/$(SERVICE_OBJS:.o=.d)
-include $(BUILD_DIR)/$(CLI_OBJS:.o=.d)

clean:
	rm -rf ./build
.PHONY: clean