#include "ipc_server.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

IpcServer& IpcServer::get_instance() {
    static IpcServer instance;
    return instance;
}

IpcServer::IpcServer() : socket_fd(-1), server_running(false) {
    const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir) {
        runtime_dir = "/tmp";
    }
    socket_path = std::string(runtime_dir) + "/linux-taskmanager.sock";
}

IpcServer::~IpcServer() {
    stop_server();
}

bool IpcServer::start_server() {
    // Create socket
    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("socket");
        return false;
    }
    
    // Remove existing socket file
    unlink(socket_path.c_str());
    
    // Bind socket
    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    if (bind(socket_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        close(socket_fd);
        return false;
    }
    
    // Listen for connections
    if (listen(socket_fd, 1) < 0) {
        perror("listen");
        close(socket_fd);
        return false;
    }
    
    server_running = true;
    return true;
}

void IpcServer::stop_server() {
    if (socket_fd >= 0) {
        close(socket_fd);
        socket_fd = -1;
    }
    unlink(socket_path.c_str());
    server_running = false;
}

bool IpcServer::is_running() const {
    return server_running;
}

std::string IpcServer::get_socket_path() const {
    return socket_path;
}