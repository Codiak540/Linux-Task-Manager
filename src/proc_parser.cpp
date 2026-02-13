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
#include <sys/sysinfo.h>

std::map<pid_t, ProcessInfo> ProcParser::last_processes;
unsigned long long ProcParser::last_system_ticks = 0;

unsigned long long get_total_ticks() {
    std::ifstream stat_file("/proc/stat");
    std::string line;
    if (!std::getline(stat_file, line)) return 0;

    std::istringstream iss(line);
    std::string cpu;
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    return user + nice + system + idle + iowait + irq + softirq + steal;
}

std::vector<ProcessInfo> ProcParser::get_all_processes() {
    std::vector<ProcessInfo> current_procs;
    unsigned long long current_system_ticks = get_total_ticks();
    unsigned long long system_delta = current_system_ticks - last_system_ticks;
    int num_cores = get_nprocs(); // Get number of CPU cores

    DIR* dir = opendir("/proc");
    if (!dir) return current_procs;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR && std::all_of(entry->d_name,
            entry->d_name + strlen(entry->d_name), ::isdigit)) {
            try {
                current_procs.push_back(parse_process(std::atoi(entry->d_name)));
            } catch (...) { continue; }
        }
    }
    closedir(dir);

    for (auto& proc : current_procs) {
        if (last_processes.count(proc.pid) && system_delta > 0) {
            long proc_delta = proc.cpu_time - last_processes[proc.pid].cpu_time;

            // Normalize by Cores: (proc_ticks / system_ticks) * 100
            // This represents % of total system capacity
            proc.cpu_usage = (static_cast<double>(proc_delta) / system_delta) * 100.0;

            // Optional: If you want "Solaris mode" (where 1 core = 100%),
            // multiply the result by num_cores.
        } else {
            proc.cpu_usage = 0.0;
        }
    }

    // Update state for next tick
    last_processes.clear();
    for (const auto& p : current_procs) last_processes[p.pid] = p;
    last_system_ticks = current_system_ticks;

    return current_procs;
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

    // Initialize memory to 0 to avoid garbage values
    info.memory_rss = 0;
    info.memory_vms = 0;
    info.thread_count = 0;
    info.user = "unknown";

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
    // Skip ahead to utime and stime (fields 14 and 15)
    // After state (field 3) and ppid (field 4), we need to skip fields 5-13 (9 fields)
    for (int i = 0; i < 9; i++) {
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
        std::string key, line_status;
        uint64_t value;

        while (std::getline(status_file, line_status)) {
            // Parse "Key:    Value    kB" format
            std::istringstream status_iss(line_status);
            status_iss >> key >> value;

            if (key == "VmRSS:") {
                // VmRSS is in kB, convert to bytes
                info.memory_rss = value * 1024;
            } else if (key == "VmSize:") {
                // VmSize is in kB, convert to bytes
                info.memory_vms = value * 1024;
            } else if (key == "Threads:") {
                info.thread_count = static_cast<int>(value);
            } else if (key == "Uid:") {
                struct passwd* pwd = getpwuid(static_cast<uid_t>(value));
                if (pwd) {
                    info.user = pwd->pw_name;
                }
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