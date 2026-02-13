#pragma once

#include <string>
#include <vector>

struct ServiceInfo {
    std::string name;
    std::string description;
    std::string state;
    std::string active;
    pid_t main_pid;
    std::string unit_type;
};

struct StartupEntry {
    std::string name;
    std::string path;
    std::string description;
    bool enabled;
    std::string source;
};

class SystemdManager {
public:
    static std::vector<ServiceInfo> get_all_services();
    static ServiceInfo get_service_info(const std::string& name);

    static bool start_service(const std::string& name);
    static bool stop_service(const std::string& name);
    static bool restart_service(const std::string& name);

    static std::vector<StartupEntry> get_startup_entries();
    static bool enable_startup(const std::string& path);
    static bool disable_startup(const std::string& path);

private:
    static std::string exec_command(const std::string& cmd);
    static bool parse_service_status(const std::string& output, ServiceInfo& info);
};