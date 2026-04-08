// server.cpp
// Complete FTP + Chat Server Implementation
// Computer Networks Assignment

#include <iostream>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctime>
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>

#define BUFFER_SIZE 8192
#define FILE_BUFFER_SIZE 65536
#define PORT 8888
#define MAX_CLIENTS 30
#define SERVER_FILE_DIR "server_files"

using namespace std;

// Structure to store client information
struct Client {
    int socket_fd;
    string username;
    bool is_authenticated;
    time_t connect_time;
    
    Client() : socket_fd(-1), username(""), is_authenticated(false), connect_time(0) {}
    
    Client(int fd, const string& name) : socket_fd(fd), username(name), 
                                          is_authenticated(true), connect_time(time(0)) {}
};

// Global variables
vector<Client> clients;
map<string, size_t> file_sizes; // Cache file sizes
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function to split string by delimiter
vector<string> split_string(const string& str, char delimiter) {
    vector<string> tokens;
    stringstream ss(str);
    string token;
    
    while (getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    
    return tokens;
}

// Function to trim whitespace
string trim_string(const string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

// Function to get current timestamp string
string get_timestamp() {
    time_t now = time(0);
    struct tm* timeinfo = localtime(&now);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", timeinfo);
    return string(buffer);
}

// Log activity to server console
void log_activity(const string& event) {
    cout << "\033[36m[" << get_timestamp() << "]\033[0m " << event << endl;
}

// Log error to server console
void log_error(const string& error) {
    cerr << "\033[31m[" << get_timestamp() << "] ERROR: " << error << "\033[0m" << endl;
}

// Find client by username
Client* find_client_by_username(const string& username) {
    for (auto& client : clients) {
        if (client.username == username && client.is_authenticated) {
            return &client;
        }
    }
    return nullptr;
}

// Find client by socket
Client* find_client_by_socket(int socket_fd) {
    for (auto& client : clients) {
        if (client.socket_fd == socket_fd) {
            return &client;
        }
    }
    return nullptr;
}

// Remove client from list
void remove_client(int socket_fd) {
    pthread_mutex_lock(&clients_mutex);
    
    auto it = find_if(clients.begin(), clients.end(), 
                      [socket_fd](const Client& c) { return c.socket_fd == socket_fd; });
    
    if (it != clients.end()) {
        string username = it->username;
        clients.erase(it);
        
        // Broadcast user left message
        string leave_msg = "SYSTEM|" + username + " has left the chat";
        for (auto& client : clients) {
            send(client.socket_fd, leave_msg.c_str(), leave_msg.length(), 0);
        }
        
        log_activity("User '" + username + "' disconnected. Total clients: " + to_string(clients.size()));
    }
    
    pthread_mutex_unlock(&clients_mutex);
}

// Send message to all clients except one
void broadcast_message(const string& message, int exclude_fd = -1) {
    pthread_mutex_lock(&clients_mutex);
    
    for (auto& client : clients) {
        if (client.socket_fd != exclude_fd && client.is_authenticated) {
            send(client.socket_fd, message.c_str(), message.length(), 0);
        }
    }
    
    pthread_mutex_unlock(&clients_mutex);
}

// Send private message
bool send_private_message(const string& target_username, const string& message) {
    Client* target = find_client_by_username(target_username);
    if (!target) {
        return false;
    }
    
    send(target->socket_fd, message.c_str(), message.length(), 0);
    return true;
}

// Create server file directory if it doesn't exist
void create_server_file_dir() {
    struct stat st;
    if (stat(SERVER_FILE_DIR, &st) == -1) {
        mkdir(SERVER_FILE_DIR, 0777);
        log_activity("Created server file directory: " + string(SERVER_FILE_DIR));
    }
}

// Get list of available files on server
string get_file_list() {
    DIR* dir;
    struct dirent* entry;
    struct stat st;
    string file_list = "FILE_LIST";
    
    dir = opendir(SERVER_FILE_DIR);
    if (dir == nullptr) {
        log_error("Cannot open server file directory");
        return "FILE_LIST|";
    }
    
    while ((entry = readdir(dir)) != nullptr) {
        string filename = entry->d_name;
        if (filename != "." && filename != "..") {
            string full_path = string(SERVER_FILE_DIR) + "/" + filename;
            if (stat(full_path.c_str(), &st) == 0) {
                size_t size_kb = st.st_size / 1024;
                if (size_kb == 0 && st.st_size > 0) size_kb = 1; // At least 1 KB for small files
                file_list += "|" + filename + ":" + to_string(size_kb);
            }
        }
    }
    
    closedir(dir);
    return file_list;
}

// Check if file exists on server
bool file_exists_on_server(const string& filename) {
    string full_path = string(SERVER_FILE_DIR) + "/" + filename;
    struct stat st;
    return (stat(full_path.c_str(), &st) == 0);
}

// Get file size
size_t get_file_size(const string& filename) {
    string full_path = string(SERVER_FILE_DIR) + "/" + filename;
    struct stat st;
    if (stat(full_path.c_str(), &st) == 0) {
        return st.st_size;
    }
    return 0;
}

// Handle LIST command - show available files
bool handle_list_command(int client_socket) {
    string file_list = get_file_list();
    send(client_socket, file_list.c_str(), file_list.length(), 0);
    log_activity("Sent file list to client");
    return true;
}

// Handle GET command - download file
bool handle_get_command(int client_socket, const string& filename) {
    string full_path = string(SERVER_FILE_DIR) + "/" + filename;
    
    if (!file_exists_on_server(filename)) {
        string error_msg = "ERROR|File not found: " + filename;
        send(client_socket, error_msg.c_str(), error_msg.length(), 0);
        log_activity("File not found: " + filename);
        return false;
    }
    
    size_t file_size = get_file_size(filename);
    string size_msg = "FILE_SIZE|" + filename + "|" + to_string(file_size);
    
    if (send(client_socket, size_msg.c_str(), size_msg.length(), 0) < 0) {
        log_error("Failed to send file size to client");
        return false;
    }
    
    // Wait for client ready signal
    char ready_buffer[BUFFER_SIZE];
    memset(ready_buffer, 0, BUFFER_SIZE);
    if (recv(client_socket, ready_buffer, BUFFER_SIZE - 1, 0) < 0) {
        log_error("No ready signal from client");
        return false;
    }
    
    string ready_str(ready_buffer);
    if (ready_str != "READY") {
        log_error("Client not ready for file transfer");
        return false;
    }
    
    // Send file
    ifstream file(full_path, ios::binary);
    if (!file.is_open()) {
        log_error("Cannot open file for reading: " + filename);
        return false;
    }
    
    char* buffer = new char[FILE_BUFFER_SIZE];
    size_t total_sent = 0;
    
    log_activity("Sending file: " + filename + " (" + to_string(file_size / 1024) + " KB)");
    
    while (total_sent < file_size) {
        size_t remaining = file_size - total_sent;
        size_t to_read = min(remaining, (size_t)FILE_BUFFER_SIZE);
        
        file.read(buffer, to_read);
        size_t bytes_read = file.gcount();
        
        if (bytes_read > 0) {
            ssize_t sent = send(client_socket, buffer, bytes_read, 0);
            if (sent < 0) {
                log_error("File send failed");
                delete[] buffer;
                file.close();
                return false;
            }
            total_sent += sent;
        }
    }
    
    delete[] buffer;
    file.close();
    
    log_activity("File sent successfully: " + filename);
    return true;
}

// Handle PUT command - upload file
bool handle_put_command(int client_socket, const string& filename, size_t file_size) {
    string full_path = string(SERVER_FILE_DIR) + "/" + filename;
    
    // Check if file already exists
    if (file_exists_on_server(filename)) {
        string error_msg = "ERROR|File already exists on server";
        send(client_socket, error_msg.c_str(), error_msg.length(), 0);
        log_activity("Upload failed - file exists: " + filename);
        return false;
    }
    
    // Send ready signal
    send(client_socket, "READY", 5, 0);
    
    // Receive file
    ofstream file(full_path, ios::binary);
    if (!file.is_open()) {
        string error_msg = "ERROR|Cannot create file on server";
        send(client_socket, error_msg.c_str(), error_msg.length(), 0);
        log_error("Cannot create file: " + filename);
        return false;
    }
    
    char* buffer = new char[FILE_BUFFER_SIZE];
    size_t total_received = 0;
    
    log_activity("Receiving file: " + filename + " (" + to_string(file_size / 1024) + " KB)");
    
    while (total_received < file_size) {
        size_t remaining = file_size - total_received;
        size_t to_receive = min(remaining, (size_t)FILE_BUFFER_SIZE);
        
        ssize_t bytes_received = recv(client_socket, buffer, to_receive, 0);
        
        if (bytes_received <= 0) {
            log_error("File receive failed");
            delete[] buffer;
            file.close();
            return false;
        }
        
        file.write(buffer, bytes_received);
        total_received += bytes_received;
    }
    
    delete[] buffer;
    file.close();
    
    // Send success confirmation
    send(client_socket, "UPLOAD_SUCCESS", 14, 0);
    
    log_activity("File uploaded successfully: " + filename);
    return true;
}

// Handle MSG command - group message
bool handle_msg_command(Client* sender, const string& message) {
    string formatted_msg = "GROUP|" + sender->username + "|" + message;
    broadcast_message(formatted_msg, sender->socket_fd);
    
    log_activity("User '" + sender->username + "' sent group message: " + message);
    return true;
}

// Handle PM command - private message
bool handle_pm_command(Client* sender, const string& target, const string& message) {
    string formatted_msg = "PRIVATE|" + sender->username + "|" + message;
    
    if (send_private_message(target, formatted_msg)) {
        log_activity("User '" + sender->username + "' sent private message to '" + target + "'");
        return true;
    } else {
        string error_msg = "ERROR|User '" + target + "' not found or offline";
        send(sender->socket_fd, error_msg.c_str(), error_msg.length(), 0);
        log_activity("Private message failed - user not found: " + target);
        return false;
    }
}

// Handle USERS command - list online users
bool handle_users_command(int client_socket) {
    string user_list = "USER_LIST";
    
    pthread_mutex_lock(&clients_mutex);
    for (auto& client : clients) {
        if (client.is_authenticated) {
            user_list += "|" + client.username;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    send(client_socket, user_list.c_str(), user_list.length(), 0);
    log_activity("Sent user list to client");
    return true;
}

// Process client command
void process_command(int client_socket, const string& command) {
    vector<string> parts = split_string(command, '|');
    
    if (parts.empty()) return;
    
    Client* client = find_client_by_socket(client_socket);
    if (!client) return;
    
    if (parts[0] == "MSG") {
        if (parts.size() >= 2) {
            handle_msg_command(client, parts[1]);
        }
    }
    else if (parts[0] == "PM") {
        if (parts.size() >= 3) {
            handle_pm_command(client, parts[1], parts[2]);
        }
    }
    else if (parts[0] == "USERS") {
        handle_users_command(client_socket);
    }
    else if (parts[0] == "LIST") {
        handle_list_command(client_socket);
    }
    else if (parts[0] == "GET") {
        if (parts.size() >= 2) {
            handle_get_command(client_socket, parts[1]);
        }
    }
    else if (parts[0] == "PUT") {
        if (parts.size() >= 3) {
            size_t file_size = stoull(parts[2]);
            handle_put_command(client_socket, parts[1], file_size);
        }
    }
    else if (parts[0] == "QUIT") {
        log_activity("User '" + client->username + "' requested quit");
        close(client_socket);
        remove_client(client_socket);
    }
}

// Handle client connection (forked process)
void handle_client(int client_socket, struct sockaddr_in client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);
    
    log_activity("New connection from " + string(client_ip) + ":" + to_string(client_port));
    
    // Receive username registration
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    
    ssize_t bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        log_error("Failed to receive username from client");
        close(client_socket);
        exit(0);
    }
    
    string reg_msg(buffer);
    vector<string> parts = split_string(reg_msg, '|');
    
    if (parts.size() < 2 || parts[0] != "REGISTER") {
        log_error("Invalid registration message from client");
        close(client_socket);
        exit(0);
    }
    
    string username = parts[1];
    
    // Check if username already taken
    pthread_mutex_lock(&clients_mutex);
    bool username_taken = (find_client_by_username(username) != nullptr);
    
    if (username_taken) {
        send(client_socket, "USERNAME_TAKEN", 14, 0);
        pthread_mutex_unlock(&clients_mutex);
        log_activity("Username taken: " + username);
        close(client_socket);
        exit(0);
    }
    
    // Add client to list
    clients.push_back(Client(client_socket, username));
    pthread_mutex_unlock(&clients_mutex);
    
    // Send success message
    string welcome_msg = "SYSTEM|Welcome to the chat, " + username + "!";
    send(client_socket, welcome_msg.c_str(), welcome_msg.length(), 0);
    
    // Broadcast user joined message
    string join_msg = "SYSTEM|" + username + " has joined the chat";
    broadcast_message(join_msg, client_socket);
    
    log_activity("User '" + username + "' registered. Total clients: " + to_string(clients.size()));
    
    // Main client communication loop
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            // Client disconnected
            log_activity("Client '" + username + "' disconnected");
            break;
        }
        
        string command(buffer);
        process_command(client_socket, command);
    }
    
    // Cleanup
    close(client_socket);
    remove_client(client_socket);
    exit(0);
}

// Signal handler for zombie processes
void sigchld_handler(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main(int argc, char* argv[]) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int port = PORT;
    
    // Get port from command line if provided
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    
    // Create server file directory
    create_server_file_dir();
    
    // Set up signal handler for zombie processes
    signal(SIGCHLD, sigchld_handler);
    
    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        log_error("Failed to create socket");
        return 1;
    }
    
    // Set socket option to reuse address
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_error("Failed to set socket options");
        close(server_socket);
        return 1;
    }
    
    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    // Bind socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        log_error("Failed to bind socket to port " + to_string(port));
        close(server_socket);
        return 1;
    }
    
    // Listen for connections
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        log_error("Failed to listen on socket");
        close(server_socket);
        return 1;
    }
    
    cout << "\033[32m=========================================\033[0m" << endl;
    cout << "\033[32m   FTP & Chat Server - Computer Networks\033[0m" << endl;
    cout << "\033[32m=========================================\033[0m" << endl;
    cout << "\033[33mServer listening on port " << port << "\033[0m" << endl;
    cout << "\033[33mFile directory: " << SERVER_FILE_DIR << "\033[0m" << endl;
    cout << "\033[33mWaiting for connections...\033[0m" << endl;
    cout << "=========================================" << endl << endl;
    
    // Main server loop
    while (true) {
        // Accept client connection
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            log_error("Failed to accept connection");
            continue;
        }
        
        // Fork to handle client
        pid_t pid = fork();
        
        if (pid < 0) {
            log_error("Failed to fork process");
            close(client_socket);
        }
        else if (pid == 0) {
            // Child process - handle client
            close(server_socket);
            handle_client(client_socket, client_addr);
            exit(0);
        }
        else {
            // Parent process - continue listening
            close(client_socket);
        }
    }
    
    // Cleanup (never reached in this implementation)
    close(server_socket);
    return 0;
}