.PHONY: clean

CXX = g++
CXXFLAGS = -std=c++11 -Wall -pthread

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

TARGET = $(BIN_DIR)/server
SRCS = $(SRC_DIR)/http_conn/http_conn.cpp \
	   $(SRC_DIR)/main/server.cpp \
	   $(SRC_DIR)/thread_pool/thread_pool.cpp
OBJS = $(patsubst $(SRC_DIR)/%/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ 

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	@echo "all done"