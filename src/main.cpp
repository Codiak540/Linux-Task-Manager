#include "task_manager.h"
#include "ipc_server.h"
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <fstream>

bool check_existing_instance(const std::string& socket_path) {
    // Try to connect to existing socket
    const int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    int ret = connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    close(sock);

    if (ret == 0) {
        std::cout << "Task manager is already running. Bringing window to foreground..." << std::endl;
        return true;
    }

    return false;
}

void create_pid_file(const std::string& socket_path) {
    std::string pid_file = socket_path + ".pid";
    std::ofstream pf(pid_file);
    pf << getpid();
    pf.close();
}

int main() {
    // Setup IPC server for single instance
    IpcServer& ipc = IpcServer::get_instance();
    std::string socket_path = ipc.get_socket_path();

    // Check if another instance is running
    if (check_existing_instance(socket_path)) {
        return 0;
    }

    // Start IPC server to claim the socket
    if (!ipc.start_server()) {
        std::cerr << "Failed to start IPC server" << std::endl;
        return 1;
    }

    create_pid_file(socket_path);

    try {
        TaskManager tm;
        tm.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}