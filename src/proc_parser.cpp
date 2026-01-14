#include "proc_parser.h"
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/resource.h>
#include <pwd.h>
#include <csignal>
#include <unistd.h>
#include <cstring>
#include <algorithm>

std::vector<ProcessInfo> ProcParser::get_all_processes() {
    std::vector<ProcessInfo> processes;
    DIR* dir = opendir("/proc");
    if (!dir) return processes;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR && std::all_of(entry->d_name,
            entry->d_name + strlen(entry->d_name), ::isdigit)) {
            pid_t pid = std::atoi(entry->d_name);
            try {
                ProcessInfo info = parse_process(pid);
                processes.push_back(info);
            } catch (...) {
                // Skip processes that disappear
            }
        }
    }
    closedir(dir);

    // Sort by PID for consistency
    std::sort(processes.begin(), processes.end(),
        [](const ProcessInfo& a, const ProcessInfo& b) { return a.pid < b.pid; });

    return processes;
}

std::vector<ProcessInfo> ProcParser::get_user_processes() {
    auto all = get_all_processes();
    std::vector<ProcessInfo> user_procs;
    uid_t uid = getuid();

    for (const auto& proc : all) {
        struct passwd* pwd = getpwnam(proc.user.c_str());
        if (pwd && pwd->pw_uid == uid) {
            user_procs.push_back(proc);
        }
    }
    return user_procs;
}

ProcessInfo ProcParser::parse_process(pid_t pid) {
    ProcessInfo info;
    info.pid = pid;
    info.is_elevated = (geteuid() == 0 && pid != getpid());

    // Read /proc/[pid]/stat
    std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream stat_file(stat_path);
    if (!stat_file) throw std::runtime_error("Cannot read stat");

    std::string line;
    std::getline(stat_file, line);

    // Find the command name which is in parentheses
    size_t paren_start = line.find('(');
    size_t paren_end = line.rfind(')');
    if (paren_start == std::string::npos || paren_end == std::string::npos) {
        throw std::runtime_error("Invalid stat format");
    }

    info.name = line.substr(paren_start + 1, paren_end - paren_start - 1);

    // Parse the rest after the closing paren
    std::istringstream iss(line.substr(paren_end + 1));
    char state;
    int ppid;
    long utime, stime;

    iss >> state >> ppid;
    // Skip ahead to utime and stime (fields 14 and 15, so skip 11 more fields)
    for (int i = 0; i < 11; i++) {
        long dummy;
        iss >> dummy;
    }
    iss >> utime >> stime;

    info.ppid = ppid;
    info.state = state;
    info.cpu_time = utime + stime;  // Total jiffies

    // Read /proc/[pid]/status for memory and threads
    std::string status_path = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream status_file(status_path);
    if (status_file) {
        std::string key, value_str;
        uint64_t value;
        while (std::getline(status_file, line)) {
            std::istringstream status_iss(line);
            status_iss >> key >> value;
            if (key == "VmRSS:") info.memory_rss = value * 1024;
            else if (key == "VmSize:") info.memory_vms = value * 1024;
            else if (key == "Threads:") info.thread_count = value;
            else if (key == "Uid:") {
                struct passwd* pwd = getpwuid(value);
                if (pwd) info.user = pwd->pw_name;
            }
        }
    }

    // Read /proc/[pid]/exe for executable path
    char exe_path[PATH_MAX];
    std::string exe_link = "/proc/" + std::to_string(pid) + "/exe";
    ssize_t len = readlink(exe_link.c_str(), exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        info.exe_path = exe_path;
    }

    // Read /proc/[pid]/cgroup for cgroup
    std::string cgroup_path = "/proc/" + std::to_string(pid) + "/cgroup";
    info.cgroup = read_file(cgroup_path);

    // These will be filled in by the caller with proper CPU calculation
    info.cpu_usage = 0;
    info.memory_usage = 0;

    return info;
}

ProcessInfo ProcParser::get_process_info(pid_t pid) {
    return parse_process(pid);
}

SystemStats ProcParser::get_system_stats() {
    SystemStats stats = {};

    // Read /proc/meminfo
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    while (std::getline(meminfo, line)) {
        std::istringstream iss(line);
        std::string key;
        uint64_t value;
        iss >> key >> value;

        if (key == "MemTotal:") stats.total_memory = value * 1024;
        else if (key == "MemAvailable:") stats.available_memory = value * 1024;
        else if (key == "Cached:") stats.cached_memory = value * 1024;
    }

    // Read /proc/uptime
    std::ifstream uptime("/proc/uptime");
    uptime >> stats.uptime;

    // Calculate total CPU usage from /proc/stat
    std::ifstream stat("/proc/stat");
    std::getline(stat, line);  // Read first line (cpu totals)

    // Parse: cpu  user nice system idle iowait irq softirq
    std::istringstream cpu_iss(line);
    std::string cpu_label;
    long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0;

    cpu_iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq;

    long total = user + nice + system + idle + iowait + irq + softirq;
    if (total > 0) {
        long busy = user + nice + system + irq + softirq;
        stats.total_cpu_usage = (busy * 100.0) / total;
    }

    return stats;
}

bool ProcParser::terminate_process(pid_t pid, bool force) {
    int sig = force ? SIGKILL : SIGTERM;
    return kill(pid, sig) == 0;
}

bool ProcParser::suspend_process(pid_t pid) {
    return kill(pid, SIGSTOP) == 0;
}

bool ProcParser::resume_process(pid_t pid) {
    return kill(pid, SIGCONT) == 0;
}

bool ProcParser::set_priority(pid_t pid, int priority) {
    return setpriority(PRIO_PROCESS, pid, priority) == 0;
}

std::string ProcParser::read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string ProcParser::get_exe_name(pid_t pid) {
    char exe_path[PATH_MAX];
    std::string link = "/proc/" + std::to_string(pid) + "/exe";
    ssize_t len = readlink(link.c_str(), exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        std::string path(exe_path);
        return path.substr(path.find_last_of("/\\") + 1);
    }
    return "";
}

std::string ProcParser::get_user_name(uid_t uid) {
    struct passwd* pwd = getpwuid(uid);
    return pwd ? pwd->pw_name : "unknown";
}