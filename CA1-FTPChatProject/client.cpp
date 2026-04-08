// client.cpp
// Complete FTP + Chat Client Implementation
// Computer Networks Assignment

#include <iostream>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <ctime>
#include <iomanip>
#include <sys/stat.h> 
#include <sys/types.h>

#define BUFFER_SIZE 8192
#define FILE_BUFFER_SIZE 65536
#define PORT 8888
#define CLIENT_FILE_DIR "test_files"

using namespace std;

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

// Function to trim whitespace from string
string trim_string(const string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

// Function to display formatted messages
void display_message(const string& type, const string& sender, const string& content) {
    time_t now = time(0);
    struct tm* timeinfo = localtime(&now);
    char timestamp[10];
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", timeinfo);
    
    cout << "\033[2K\r"; // Clear current line
    cout.flush();
    
    if (type == "GROUP") {
        cout << "\033[36m[" << timestamp << "] \033[32m[Group]\033[0m " 
             << sender << ": " << content << endl;
    }
    else if (type == "PRIVATE") {
        cout << "\033[36m[" << timestamp << "] \033[35m[Private]\033[0m " 
             << sender << ": " << content << endl;
    }
    else if (type == "SYSTEM") {
        cout << "\033[36m[" << timestamp << "] \033[33m[System]\033[0m " 
             << content << endl;
    }
    else if (type == "ERROR") {
        cout << "\033[36m[" << timestamp << "] \033[31m[Error]\033[0m " 
             << content << endl;
    }
    else if (type == "FILE") {
        cout << "\033[36m[" << timestamp << "] \033[34m[File]\033[0m " 
             << content << endl;
    }
    else if (type == "YOUR_MSG") {
        cout << "\033[36m[" << timestamp << "] \033[32m[You]\033[0m " 
             << content << endl;
    }
    
    cout << "\033[32m> \033[0m"; // Print prompt
    cout.flush();
}

// Function to display file download progress
void display_progress(size_t current, size_t total, const string& filename) {
    int percentage = (current * 100) / total;
    int bar_width = 40;
    int filled = (percentage * bar_width) / 100;
    
    cout << "\r\033[2K"; // Clear line
    cout << "Downloading " << filename << ": [";
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) cout << "=";
        else if (i == filled) cout << ">";
        else cout << " ";
    }
    cout << "] " << percentage << "% (" << (current / 1024) 
         << "/" << (total / 1024) << " KB)";
    cout.flush();
}

// Function to handle file upload
bool upload_file(int sock, const string& filename) {
    // Build full path using test_files folder
    string full_path = string(CLIENT_FILE_DIR) + "/" + filename;
    
    ifstream file(full_path, ios::binary | ios::ate);
    
    if (!file.is_open()) {
        display_message("ERROR", "", "Cannot open file: " + full_path);
        return false;
    }
    
    // Get file size
    streamsize file_size = file.tellg();
    file.seekg(0, ios::beg);
    
    if (file_size == 0) {
        display_message("ERROR", "", "File is empty: " + filename);
        file.close();
        return false;
    }
    
    // Send PUT command with filename and size
    string put_cmd = "PUT|" + filename + "|" + to_string(file_size);
    if (send(sock, put_cmd.c_str(), put_cmd.length(), 0) < 0) {
        display_message("ERROR", "", "Failed to send upload command");
        file.close();
        return false;
    }
    
    // Wait for server ready signal
    char response[BUFFER_SIZE];
    memset(response, 0, BUFFER_SIZE);
    if (recv(sock, response, BUFFER_SIZE - 1, 0) < 0) {
        display_message("ERROR", "", "No response from server");
        file.close();
        return false;
    }
    
    string resp_str(response);
    if (resp_str != "READY") {
        display_message("ERROR", "", "Server not ready: " + resp_str);
        file.close();
        return false;
    }
    
    // Send file data in chunks
    char* buffer = new char[FILE_BUFFER_SIZE];
    size_t total_sent = 0;
    
    display_message("FILE", "", "Uploading " + filename + " (" + 
                   to_string(file_size / 1024) + " KB)...");
    
    while (total_sent < (size_t)file_size) {
        size_t remaining = file_size - total_sent;
        size_t to_send = min(remaining, (size_t)FILE_BUFFER_SIZE);
        
        file.read(buffer, to_send);
        size_t bytes_read = file.gcount();
        
        if (bytes_read > 0) {
            ssize_t sent = send(sock, buffer, bytes_read, 0);
            if (sent < 0) {
                display_message("ERROR", "", "Upload failed during transfer");
                delete[] buffer;
                file.close();
                return false;
            }
            total_sent += sent;
            
            // Display progress
            int percentage = (total_sent * 100) / file_size;
            cout << "\r\033[2K";
            cout << "Uploading: " << percentage << "% (" 
                 << (total_sent / 1024) << "/" << (file_size / 1024) << " KB)";
            cout.flush();
        }
    }
    
    cout << endl;
    delete[] buffer;
    file.close();
    
    // Wait for upload confirmation
    memset(response, 0, BUFFER_SIZE);
    if (recv(sock, response, BUFFER_SIZE - 1, 0) < 0) {
        display_message("ERROR", "", "No upload confirmation");
        return false;
    }
    
    resp_str = response;
    if (resp_str == "UPLOAD_SUCCESS") {
        display_message("FILE", "", "Upload completed successfully!");
        return true;
    } else {
        display_message("ERROR", "", "Upload failed: " + resp_str);
        return false;
    }
}
// Function to handle file download

// Function to handle file download
bool download_file(int sock, const string& filename) {
    // Send GET command
    string get_cmd = "GET|" + filename;
    if (send(sock, get_cmd.c_str(), get_cmd.length(), 0) < 0) {
        display_message("ERROR", "", "Failed to send download command");
        return false;
    }
    
    // Receive response (FILE_SIZE or ERROR)
    char response[BUFFER_SIZE];
    memset(response, 0, BUFFER_SIZE);
    if (recv(sock, response, BUFFER_SIZE - 1, 0) < 0) {
        display_message("ERROR", "", "No response from server");
        return false;
    }
    
    string resp_str(response);
    vector<string> parts = split_string(resp_str, '|');
    
    if (parts[0] == "ERROR") {
        display_message("ERROR", "", parts[1]);
        return false;
    }
    
    if (parts[0] != "FILE_SIZE") {
        display_message("ERROR", "", "Unexpected response: " + resp_str);
        return false;
    }
    
    if (parts.size() < 3) {
        display_message("ERROR", "", "Invalid file info received");
        return false;
    }
    
    string file_name = parts[1];
    size_t file_size = stoull(parts[2]);
    
    // Create test_files directory if it doesn't exist
    struct stat st;
    if (stat(CLIENT_FILE_DIR, &st) == -1) {
        mkdir(CLIENT_FILE_DIR, 0777);
        display_message("FILE", "", "Created directory: " + string(CLIENT_FILE_DIR));
    }
    
    // Build full path using test_files folder
    string full_path = string(CLIENT_FILE_DIR) + "/" + file_name;
    
    // Open file for writing
    ofstream outfile(full_path, ios::binary);
    if (!outfile.is_open()) {
        display_message("ERROR", "", "Cannot create file: " + full_path);
        return false;
    }
    
    // Send ACK to start transfer
    send(sock, "READY", 5, 0);
    
    // Receive file data
    char* file_buffer = new char[FILE_BUFFER_SIZE];
    size_t total_received = 0;
    size_t remaining = file_size;
    
    display_message("FILE", "", "Downloading " + filename + " (" + 
                   to_string(file_size / 1024) + " KB)...");
    
    while (total_received < file_size) {
        size_t to_receive = min(remaining, (size_t)FILE_BUFFER_SIZE);
        ssize_t bytes_received = recv(sock, file_buffer, to_receive, 0);
        
        if (bytes_received <= 0) {
            display_message("ERROR", "", "Download interrupted");
            delete[] file_buffer;
            outfile.close();
            return false;
        }
        
        outfile.write(file_buffer, bytes_received);
        total_received += bytes_received;
        remaining -= bytes_received;
        
        // Display progress
        display_progress(total_received, file_size, file_name);
    }
    
    cout << endl;
    delete[] file_buffer;
    outfile.close();
    
    display_message("FILE", "", "Download completed successfully! Saved to: " + full_path);
    return true;
}

// Function to send command and get response
string send_command(int sock, const string& command) {
    if (send(sock, command.c_str(), command.length(), 0) < 0) {
        return "ERROR|Failed to send command";
    }
    
    char response[BUFFER_SIZE];
    memset(response, 0, BUFFER_SIZE);
    
    if (recv(sock, response, BUFFER_SIZE - 1, 0) < 0) {
        return "ERROR|No response from server";
    }
    
    return string(response);
}

int main(int argc, char* argv[]) {
    string server_ip = "127.0.0.1";
    int port = PORT;
    string username;
    
    // Get server IP from command line if provided
    if (argc > 1) {
        server_ip = argv[1];
    }
    if (argc > 2) {
        port = atoi(argv[2]);
    }
    
    // Get username
    cout << "=========================================" << endl;
    cout << "   FTP & Chat Client - Computer Networks" << endl;
    cout << "=========================================" << endl;
    cout << "Enter your username: ";
    getline(cin, username);
    
    username = trim_string(username);
    if (username.empty()) {
        cerr << "Username cannot be empty!" << endl;
        return 1;
    }
    
    // Create socket
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        cerr << "Failed to create socket" << endl;
        return 1;
    }
    
    // Configure server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);
    
    // Connect to server
    cout << "Connecting to " << server_ip << ":" << port << "... ";
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Failed to connect" << endl;
        close(client_socket);
        return 1;
    }
    cout << "Connected!" << endl;
    
    // Send username to server
    string register_msg = "REGISTER|" + username;
    if (send(client_socket, register_msg.c_str(), register_msg.length(), 0) < 0) {
        cerr << "Failed to register username" << endl;
        close(client_socket);
        return 1;
    }
    
    // Get registration confirmation
    char reg_response[BUFFER_SIZE];
    memset(reg_response, 0, BUFFER_SIZE);
    if (recv(client_socket, reg_response, BUFFER_SIZE - 1, 0) < 0) {
        cerr << "Failed to get registration confirmation" << endl;
        close(client_socket);
        return 1;
    }
    
    string reg_str(reg_response);
    if (reg_str == "USERNAME_TAKEN") {
        cout << "Username already taken! Please restart and choose another." << endl;
        close(client_socket);
        return 1;
    }
    
    cout << "\n=========================================" << endl;
    cout << "   Connected as: " << username << endl;
    cout << "   Type commands below (type HELP for help)" << endl;
    cout << "=========================================\n" << endl;
    display_message("SYSTEM", "", "Welcome to the chat! Type HELP for available commands.");
    
    // Main loop - handle both user input and incoming messages
    fd_set read_fds;
    int max_fd = max(client_socket, STDIN_FILENO) + 1;
    bool connected = true;
    string input_buffer;
    
    while (connected) {
        FD_ZERO(&read_fds);
        FD_SET(client_socket, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        
        // Wait for activity
        if (select(max_fd, &read_fds, NULL, NULL, NULL) < 0) {
            display_message("ERROR", "", "Select error");
            break;
        }
        
        // Check for incoming messages from server
        if (FD_ISSET(client_socket, &read_fds)) {
            char buffer[BUFFER_SIZE];
            memset(buffer, 0, BUFFER_SIZE);
            
            ssize_t bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
            
            if (bytes_received <= 0) {
                // Server disconnected
                display_message("SYSTEM", "", "Server disconnected!");
                connected = false;
                break;
            }
            
            string message(buffer);
            vector<string> parts = split_string(message, '|');
            
            if (parts.size() >= 1) {
                if (parts[0] == "GROUP") {
                    if (parts.size() >= 3) {
                        display_message("GROUP", parts[1], parts[2]);
                    }
                }
                else if (parts[0] == "PRIVATE") {
                    if (parts.size() >= 3) {
                        display_message("PRIVATE", parts[1], parts[2]);
                    }
                }
                else if (parts[0] == "SYSTEM") {
                    if (parts.size() >= 2) {
                        display_message("SYSTEM", "", parts[1]);
                    }
                }
                else if (parts[0] == "USER_LIST") {
                    cout << "\033[2K\r";
                    cout.flush();
                    cout << "\n=== Online Users ===" << endl;
                    for (size_t i = 1; i < parts.size(); i++) {
                        cout << "• " << parts[i] << endl;
                    }
                    cout << "===================\n" << endl;
                    cout << "\033[32m> \033[0m";
                    cout.flush();
                }
                else if (parts[0] == "FILE_LIST") {
                    cout << "\033[2K\r";
                    cout.flush();
                    cout << "\n=== Available Files ===" << endl;
                    for (size_t i = 1; i < parts.size(); i++) {
                        vector<string> file_info = split_string(parts[i], ':');
                        if (file_info.size() >= 2) {
                            cout << "• " << file_info[0] << " (" << file_info[1] << " KB)" << endl;
                        }
                    }
                    cout << "=====================\n" << endl;
                    cout << "\033[32m> \033[0m";
                    cout.flush();
                }
                else if (parts[0] == "FILE_TRANSFER") {
                    // Handle file transfer commands
                    if (parts.size() >= 3 && parts[1] == "GET") {
                        download_file(client_socket, parts[2]);
                    }
                }
            }
        }
        
        // Check for user input
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            string command_line;
            getline(cin, command_line);
            
            if (command_line.empty()) {
                cout << "\033[32m> \033[0m";
                cout.flush();
                continue;
            }
            
            command_line = trim_string(command_line);
            
            // Parse command
            stringstream ss(command_line);
            string command;
            ss >> command;
            
            // Convert to uppercase for command matching
            string upper_cmd = command;
            for (auto& c : upper_cmd) c = toupper(c);
            
            if (upper_cmd == "HELP") {
                cout << "\n=== Available Commands ===" << endl;
                cout << "MSG <message>     - Send message to all users" << endl;
                cout << "PM <user> <msg>   - Send private message to a user" << endl;
                cout << "USERS             - Show list of online users" << endl;
                cout << "LIST              - Show list of available files" << endl;
                cout << "GET <filename>    - Download a file from server" << endl;
                cout << "PUT <filename>    - Upload a file to server" << endl;
                cout << "QUIT              - Exit the program" << endl;
                cout << "HELP              - Show this help message" << endl;
                cout << "========================\n" << endl;
                cout << "\033[32m> \033[0m";
                cout.flush();
                continue;
            }
            else if (upper_cmd == "MSG") {
                string message;
                getline(ss, message);
                message = trim_string(message);
                
                if (message.empty()) {
                    display_message("ERROR", "", "Message cannot be empty");
                } else {
                    string full_cmd = "MSG|" + message;
                    if (send(client_socket, full_cmd.c_str(), full_cmd.length(), 0) < 0) {
                        display_message("ERROR", "", "Failed to send message");
                    } else {
                        // Show own message locally
                        display_message("YOUR_MSG", "", message);
                    }
                }
            }
            else if (upper_cmd == "PM") {
                string target_user;
                ss >> target_user;
                string message;
                getline(ss, message);
                message = trim_string(message);
                
                if (target_user.empty() || message.empty()) {
                    display_message("ERROR", "", "Usage: PM <username> <message>");
                } else {
                    string full_cmd = "PM|" + target_user + "|" + message;
                    if (send(client_socket, full_cmd.c_str(), full_cmd.length(), 0) < 0) {
                        display_message("ERROR", "", "Failed to send private message");
                    } else {
                        display_message("YOUR_MSG", "", "[Private to " + target_user + "] " + message);
                    }
                }
            }
            else if (upper_cmd == "USERS") {
                string full_cmd = "USERS";
                if (send(client_socket, full_cmd.c_str(), full_cmd.length(), 0) < 0) {
                    display_message("ERROR", "", "Failed to request user list");
                }
            }
            else if (upper_cmd == "LIST") {
                string full_cmd = "LIST";
                if (send(client_socket, full_cmd.c_str(), full_cmd.length(), 0) < 0) {
                    display_message("ERROR", "", "Failed to request file list");
                }
            }
            else if (upper_cmd == "GET") {
                string filename;
                ss >> filename;
                filename = trim_string(filename);
                
                if (filename.empty()) {
                    display_message("ERROR", "", "Usage: GET <filename>");
                } else {
                    download_file(client_socket, filename);
                }
            }
            else if (upper_cmd == "PUT") {
                string filename;
                ss >> filename;
                filename = trim_string(filename);
                
                if (filename.empty()) {
                    display_message("ERROR", "", "Usage: PUT <filename>");
                } else {
                    upload_file(client_socket, filename);
                }
            }
            else if (upper_cmd == "QUIT") {
                string full_cmd = "QUIT";
                send(client_socket, full_cmd.c_str(), full_cmd.length(), 0);
                display_message("SYSTEM", "", "Disconnecting...");
                connected = false;
                break;
            }
            else {
                display_message("ERROR", "", "Unknown command. Type HELP for available commands.");
            }
        }
    }
    
    // Cleanup
    close(client_socket);
    cout << "\nDisconnected from server. Goodbye!" << endl;
    
    return 0;
}