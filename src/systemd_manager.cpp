#include "systemd_manager.h"
#include "debug.h"
#include <iostream>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <dirent.h>

std::vector<ServiceInfo> SystemdManager::get_all_services() {
    std::vector<ServiceInfo> services;
    
    // Use systemctl to list services - simpler approach
    std::string output = exec_command("systemctl list-units --type=service --no-pager 2>&1 | grep service");

    DEBUG_ACTION(std::cerr << "DEBUG systemctl output:\n" << output << std::endl);

    std::istringstream iss(output);
    std::string line;

    int count = 0;
    while (std::getline(iss, line) && count < 20) {
        if (line.empty() || line.find("service") == std::string::npos) continue;

        count++;

        // Parse line like: "ssh.service loaded active running OpenSSH"
        std::istringstream line_stream(line);
        std::string name, loaded, active, sub, description;

        line_stream >> name >> loaded >> active >> sub;
        std::getline(line_stream, description);

        if (name.empty()) continue;

        ServiceInfo info;
        info.name = name;
        info.state = loaded;
        info.active = active;
        if (!description.empty() && description[0] == ' ') {
            info.description = description.substr(1);
        }
        info.main_pid = 0;

        services.push_back(info);
        DEBUG_ACTION(std::cerr << "DEBUG: Parsed service " << name << " state=" << loaded << " active=" << active << std::endl);
    }

    DEBUG_ACTION(std::cerr << "DEBUG: Total services parsed: " << services.size() << std::endl);
    return services;
}

ServiceInfo SystemdManager::get_service_info(const std::string& name) {
    ServiceInfo info;

    std::string cmd = "systemctl status " + name;
    std::string output = exec_command(cmd);
    parse_service_status(output, info);

    return info;
}

bool SystemdManager::start_service(const std::string& name) {
    std::string cmd = "systemctl start " + name;
    int ret = system(cmd.c_str());
    return ret == 0;
}

bool SystemdManager::stop_service(const std::string& name) {
    std::string cmd = "systemctl stop " + name;
    int ret = system(cmd.c_str());
    return ret == 0;
}

bool SystemdManager::restart_service(const std::string& name) {
    std::string cmd = "systemctl restart " + name;
    int ret = system(cmd.c_str());
    return ret == 0;
}

std::vector<StartupEntry> SystemdManager::get_startup_entries() {
    std::vector<StartupEntry> entries;

    // Check user-specific autostart
    const char* home = getenv("HOME");
    if (!home) return entries;

    std::string autostart_dir = std::string(home) + "/.config/autostart";
    DIR* dir = opendir(autostart_dir.c_str());
    if (!dir) return entries;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_REG) continue;

        std::string filename = entry->d_name;
        if (filename.find(".desktop") == std::string::npos) continue;

        StartupEntry startup;
        startup.name = filename.substr(0, filename.find(".desktop"));
        startup.path = autostart_dir.append("/").append(filename);
        startup.source = "User autostart";
        startup.enabled = true;

        entries.push_back(startup);
    }
    closedir(dir);

    // Check systemd user services with WantedBy=default.target
    std::string user_services = std::string(home) + "/.config/systemd/user";
    dir = opendir(user_services.c_str());
    if (dir) {
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_type != DT_REG) continue;

            std::string filename = entry->d_name;
            if (filename.find(".service") == std::string::npos) continue;

            StartupEntry startup;
            startup.name = filename.substr(0, filename.find(".service"));
            startup.path = user_services.append("/").append(filename);
            startup.source = "User systemd";
            startup.enabled = true;

            entries.push_back(startup);
        }
        closedir(dir);
    }

    return entries;
}

bool SystemdManager::enable_startup(const std::string& path) {
    // For .desktop files, remove Hidden=true
    std::ifstream file(path);
    std::string content, line;
    bool found_hidden = false;

    while (std::getline(file, line)) {
        if (line.find("Hidden=") == 0) {
            content += "Hidden=false\n";
            found_hidden = true;
        } else {
            content += line + "\n";
        }
    }
    file.close();

    if (!found_hidden) {
        content += "Hidden=false\n";
    }

    std::ofstream out(path);
    out << content;
    out.close();

    return true;
}

bool SystemdManager::disable_startup(const std::string& path) {
    // For .desktop files, add Hidden=true
    std::ifstream file(path);
    std::string content, line;
    bool found_hidden = false;

    while (std::getline(file, line)) {
        if (line.find("Hidden=") == 0) {
            content += "Hidden=true\n";
            found_hidden = true;
        } else {
            content += line + "\n";
        }
    }
    file.close();

    if (!found_hidden) {
        content += "Hidden=true\n";
    }

    std::ofstream out(path);
    out << content;
    out.close();

    return true;
}

std::string SystemdManager::exec_command(const std::string& cmd) {
    // Suppress both stderr and stdout warnings
    std::string full_cmd = cmd + " 2>/dev/null";
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) return "";

    std::string result;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);

    return result;
}

bool SystemdManager::parse_service_status(const std::string& output, ServiceInfo& info) {
    std::istringstream iss(output);
    std::string line;

    while (std::getline(iss, line)) {
        if (line.find("Active:") != std::string::npos) {
            size_t pos = line.find("Active:");
            info.active = line.substr(pos + 7);
        }
        if (line.find("Loaded:") != std::string::npos) {
            size_t pos = line.find("Loaded:");
            info.state = line.substr(pos + 7);
        }
    }

    return true;
}