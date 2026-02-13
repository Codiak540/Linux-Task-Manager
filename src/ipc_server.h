#pragma once

#include <string>

class IpcServer {
public:
    static IpcServer& get_instance();

    bool start_server();
    void stop_server();
    bool is_running() const;

    std::string get_socket_path() const;

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

private:
    IpcServer();
    ~IpcServer();

    int socket_fd = -1;
    std::string socket_path;
    bool server_running = false;
};