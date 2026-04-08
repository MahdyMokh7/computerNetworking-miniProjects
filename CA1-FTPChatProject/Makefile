# Makefile for FTP + Chat Project
# Computer Networks Assignment

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra
SERVER_FLAGS = -pthread
CLIENT_FLAGS = 

# Target names
SERVER_TARGET = server
CLIENT_TARGET = client

# Source files
SERVER_SRC = server.cpp
CLIENT_SRC = client.cpp

# Default target (runs when you just type 'make')
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# Build server
$(SERVER_TARGET): $(SERVER_SRC)
	@echo "Compiling server..."
	$(CXX) $(CXXFLAGS) -o $(SERVER_TARGET) $(SERVER_SRC) $(SERVER_FLAGS)
	@echo "Server compiled successfully!"

# Build client
$(CLIENT_TARGET): $(CLIENT_SRC)
	@echo "Compiling client..."
	$(CXX) $(CXXFLAGS) -o $(CLIENT_TARGET) $(CLIENT_SRC) $(CLIENT_FLAGS)
	@echo "Client compiled successfully!"

# Clean build files
clean:
	@echo "🧹 Cleaning up..."
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET)
	@echo "✅ Clean complete!"

# Run server (default port 8888)
run-server: $(SERVER_TARGET)
	@echo "Starting server on port 8888..."
	./$(SERVER_TARGET)

# Run server on custom port (usage: make run-server-port PORT=8889)
run-server-port: $(SERVER_TARGET)
	@echo "Starting server on port $(PORT)..."
	./$(SERVER_TARGET) $(PORT)

# Run client (connects to localhost:8888)
run-client: $(CLIENT_TARGET)
	@echo "Starting client..."
	./$(CLIENT_TARGET)

# Run client with custom server (usage: make run-client-custom IP=192.168.1.100 PORT=8888)
run-client-custom: $(CLIENT_TARGET)
	@echo "Starting client connecting to $(IP):$(PORT)..."
	./$(CLIENT_TARGET) $(IP) $(PORT)

# Run both server and client (server in background)
run-both: $(SERVER_TARGET) $(CLIENT_TARGET)
	@echo "Starting server in background..."
	@./$(SERVER_TARGET) &
	@echo "⏳ Waiting 2 seconds for server to start..."
	@sleep 2
	@echo "Starting client..."
	@./$(CLIENT_TARGET)

# Create server files directory
setup:
	@echo "Creating server_files directory..."
	@mkdir -p server_files
	@echo "Creating test file..."
	@echo "This is a test file for download" > server_files/test.txt
	@echo "Setup complete!"

# Help menu
help:
	@echo "========================================="
	@echo "   FTP + Chat Project - Makefile Commands"
	@echo "========================================="
	@echo ""
	@echo "📦 Build Commands:"
	@echo "  make          - Compile both server and client"
	@echo "  make server   - Compile only server"
	@echo "  make client   - Compile only client"
	@echo "  make clean    - Remove compiled binaries"
	@echo ""
	@echo "🚀 Run Commands:"
	@echo "  make run-server              - Run server on port 8888"
	@echo "  make run-server-port PORT=8889 - Run server on custom port"
	@echo "  make run-client              - Run client (localhost:8888)"
	@echo "  make run-client-custom IP=192.168.1.100 PORT=8888"
	@echo "  make run-both                - Run server (background) + client"
	@echo ""
	@echo "🛠️  Utility Commands:"
	@echo "  make setup    - Create server_files directory and test file"
	@echo "  make help     - Show this help menu"
	@echo ""

# Phony targets (not actual files)
.PHONY: all clean run-server run-server-port run-client run-client-custom run-both setup help

# Default target if no arguments given
.DEFAULT_GOAL := all