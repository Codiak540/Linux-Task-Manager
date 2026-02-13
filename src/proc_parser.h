#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>

struct ProcessInfo {
    pid_t pid;
    pid_t ppid;
    std::string name;
    std::string exe_path;
    std::string user;
    double cpu_usage;
    double memory_usage;
    uint64_t memory_rss;
    uint64_t memory_vms;
    int64_t cpu_time;
    int thread_count;
    std::string state;
    int nice;
    bool is_elevated;
    std::string cgroup;
};

struct SystemStats {
    double total_cpu_usage;
    uint64_t total_memory;
    uint64_t available_memory;
    uint64_t cached_memory;
    double uptime;
};

class ProcParser {
public:
    static std::vector<ProcessInfo> get_all_processes();
    static std::vector<ProcessInfo> get_user_processes();
    static ProcessInfo get_process_info(pid_t pid);
    static SystemStats get_system_stats();

    static bool terminate_process(pid_t pid, bool force = false);
    static bool suspend_process(pid_t pid);
    static bool resume_process(pid_t pid);
    static bool set_priority(pid_t pid, int priority);

private:
    static ProcessInfo parse_process(pid_t pid);
    static std::string read_file(const std::string& path);
    double calculate_cpu_usage(const ProcessInfo& before, const ProcessInfo& after);
    static std::string get_exe_name(pid_t pid);
    static std::string get_user_name(uid_t uid);

    std::map<pid_t, ProcessInfo> last_processes;
};