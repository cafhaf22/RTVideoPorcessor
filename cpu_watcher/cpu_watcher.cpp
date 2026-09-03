#include <iostream>
#include <string>
#include <atomic>
#include <csignal>
#include <ctime>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <syslog.h>
#include "common/common.h"

namespace {

std::atomic<bool> g_should_stop{false};

void SignalHandler(int signum) {
    syslog(LOG_INFO, "Received signal: %d", signum);
    g_should_stop.store(true);
}
} // namespace

class CpuWatcher {
public:
    explicit CpuWatcher(pid_t pid) : pid_(pid) {
        openlog("CpuWatcher", LOG_PID | LOG_NDELAY, LOG_USER);
        syslog(LOG_INFO, "CpuWatcher created, monitoring process: %d", pid_);
    }

    ~CpuWatcher() {
        syslog(LOG_INFO, "Exiting CpuWatcher");
    }

    void Watch() {
        uint64_t prev_system_cpu_time = 0; 
        uint64_t prev_process_cpu_time = 0;
        
        if(ReadProcessCpuTime(prev_process_cpu_time) != Status::OK || ReadSystemCpuTime(prev_system_cpu_time) != Status::OK) {
            syslog(LOG_WARNING, "Failed to obtain cpu times");
        }
        
        const std::chrono::seconds interval(1);

        while(!g_should_stop.load()) {
            auto next_wake_time = std::chrono::steady_clock::now() + interval;
            std::this_thread::sleep_until(next_wake_time);
            
            // once is awake obtain the current system and process cpu times and the used memory 
            uint64_t system_cpu_time = 0, process_cpu_time = 0;
            uint64_t process_mem_kb = 0, sys_total_kb = 0, sys_available_kb = 0;
            if(ReadProcessCpuTime(process_cpu_time) != Status::OK || 
                ReadSystemCpuTime(system_cpu_time) != Status::OK ||
                ReadProcessMemoryKb(process_mem_kb) != Status::OK ||
                ReadSystemMemoryKb(sys_total_kb, sys_available_kb) != Status::OK) {
                    syslog(LOG_WARNING, "Failed to read one or more /proc values this cycle");
                    continue;
            }

            // Do the calculations
            double cpu_percent = CalculateCpuPercentage(
                process_cpu_time - prev_process_cpu_time, system_cpu_time - prev_system_cpu_time);
            uint64_t sys_used_kb = sys_total_kb - sys_available_kb;

            // Print the statistics
            std::cout << "[Video Clipper] CPU: " << cpu_percent << "%  |  "
                << "Mem: " << process_mem_kb << " KB   ||   "
                << "[System] Mem: " << sys_used_kb << "/" << sys_total_kb << " KB" << std::endl;

            // update previos system and process cpu time's
            prev_system_cpu_time = system_cpu_time;
            prev_process_cpu_time = process_cpu_time;
        }
    }

private:
    Status ReadProcessCpuTime(uint64_t& process_cpu_time) {
        std::string path = "/proc/" + std::to_string(pid_) + "/stat";
        std::ifstream file(path);
        if(!file.is_open()) {
            syslog(LOG_ERR, "Failed to open %s", path.c_str());
            return Status::Failure;
        }

        std::string line;
        std::getline(file, line);

        // looking for field (comm) because it has () and may contain spaces
        // skip to the closeing ) then split by spaces
        size_t close_paren = line.rfind(')');
        std::istringstream rest(line.substr(close_paren + 2));

        std::string field;
        uint64_t utime = 0, stime = 0;

        // utime is field 14 and stime field 15
        // since we are at field 2, start counting at 3
        for(int field_num = 3; field_num <= 15; ++field_num) {
            rest >> field;
            if (field_num == 14) utime = std::stoull(field);
            if (field_num == 15) stime = std::stoull(field);
        }
        process_cpu_time = utime + stime;

        return Status::OK;
    }

    Status ReadSystemCpuTime(uint64_t& system_cpu_time) {
        std::ifstream file("/proc/stat");
        if (!file.is_open()) {
            syslog(LOG_ERR, "Failed to open /proc/stat");
            return Status::Failure;
        }

        std::string line;
        std::getline(file, line);  //reading only the first line

        std::istringstream rest(line);
        std::string label;
        rest >> label;  // discard "cpu"

        uint64_t total = 0;
        uint64_t value;
        // add all the values contained in the first line
        while(rest >> value) {
            total += value;
        }

        system_cpu_time = total;
        return Status::OK;
    }

    Status ReadProcessMemoryKb(uint64_t& process_memory) {
        std::string path = "/proc/" + std::to_string(pid_) + "/status";
        std::ifstream file(path);
        if (!file.is_open()) {
            syslog(LOG_ERR, "Failed to open %s", path.c_str());
            return Status::Failure;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (line.rfind("VmRSS:", 0) == 0) {
                std::istringstream rest(line.substr(6));
                rest >> process_memory;
                return Status::OK;
            }
        }
        syslog(LOG_WARNING, "VmRSS not found in %s", path.c_str());
        return Status::Failure;
    }

    Status ReadSystemMemoryKb(uint64_t& total_kb, uint64_t& available_kb) {
        std::ifstream file("/proc/meminfo");
        if(!file.is_open()) {
            syslog(LOG_ERR, "Failed to open /proc/meminfo");
            return Status::Failure;
        }

        std::string line;
        bool found_total = false, found_available = false;
        while(std::getline(file, line)) {
            if(line.rfind("MemTotal:", 0) == 0) {
                std::istringstream(line.substr(9)) >> total_kb;
                found_total = true;
            } else if(line.rfind("MemAvailable:", 0) == 0) {
                std::istringstream(line.substr(13)) >> available_kb;
                found_available = true;
            }
        }

        if(!found_total || !found_available) {
            syslog(LOG_WARNING, "MemTotal/MemAvailable not found in /proc/meminfo");
            return Status::Failure;
        }

        return Status::OK;
    }

    double CalculateCpuPercentage(const uint64_t& process_delta, const uint64_t& system_delta) {
        if (system_delta == 0) {
            return 0.0;
        }
        return (static_cast<double>(process_delta) / system_delta) * 100.0;
    }

    pid_t pid_;
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <pid_to_monitor>" << std::endl;
        return 1;
    }

    std::signal(SIGINT, SignalHandler);

    pid_t target_pid = static_cast<pid_t>(std::stoi(argv[1]));
    CpuWatcher watcher(target_pid);
    watcher.Watch();

    return 0;
}