#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

union thread_args {
    struct {
        uint32_t valid : 1;
        uint32_t specific : 1;
        uint32_t cpu_id : 6;
        uint32_t priority : 7;
        uint32_t rate : 14;
        uint32_t reserved : 3;
    }; /* data */
    // set cpu_id = 63
    uint32_t buffer_[1] {0x3f000000};
};

using spacetime_t = std::chrono::microseconds;
using cmd_job_t = std::function<void(std::vector<thread_args>&, thread_args&)>;

constexpr float g_rate_base = 10000.0f;
constexpr spacetime_t g_interval_us = spacetime_t(10000);
int g_hardware_threads = std::thread::hardware_concurrency();
std::string g_helper_str =
// unix style helper
"\033[1mNAME\033[0m\n"
"    cpuocup - set cpu rate\n"
"\033[1mSYNOPSIS\033[0m\n"
"    \033[1mcpuocup\033[0m [\033[4mcpu_id\033[0m,\033[4mrate\033[0m] [\033[4mrate\033[0m] [\033[4mcmd\033[0m,\033[4mrate\033[0m] ...\n"
"\033[1mDESCRIPTION\033[0m\n"
"    This program is used to set cpu rate. Max " + std::to_string(g_hardware_threads) + " threads are supported.\n"
"    \033[1mcpu_id\033[0m: cpu id, -1 means thread not bind any cpu, range: [-1, " + std::to_string(g_hardware_threads-1) + "]\n"
"    \033[1mrate\033[0m: thread rate, 0.0 <= rate <= 1.0\n"
"    \033[1mcmd\033[0m: f, r, F, R\n"
"        f: set rate to all threads\n"
"        r: set rate to all threads which rate is not set\n"
"        F: set rate to all threads, and bind to corresponding cpu\n"
"        R: set rate to all threads which rate is not set, and bind to corresponding cpu\n"
"\033[1mEXAMPLE\033[0m\n"
"    \033[1mcpuocup\033[0m 0,0.5 1,0.5\n"
"        set thread0 and thread1 to 50% rate, and bind thread0 to cpu0, thread1 to cpu1\n"
"    \033[1mcpuocup\033[0m 0,0.5 1,0.5 f,0.1\n"
"        set all threads to 10% rate\n"
"    \033[1mcpuocup\033[0m 0,0.5 1,0.5 r,0.1\n"
"        set thread0 and thread1 to 50% rate, and bind thread0 to cpu0, thread1 to cpu1\n"
"        and then set all threads which rate is not set to 10% rate\n"
"    \033[1mcpuocup\033[0m 0,0.5 1,0.5 F,0.1\n"
"        set all threads to 10% rate, and bind to corresponding cpu\n"
"    \033[1mcpuocup\033[0m 0,0.5 1,0.5 R,0.1\n"
"        set thread0 and thread1 to 50% rate, and bind thread0 to cpu0, thread1 to cpu1\n"
"        and then set all threads which rate is not set to 10% rate, and bind to corresponding cpu\n";

void process(double rate) {
    int process_time = g_interval_us.count() * rate;
    int sleep_time = g_interval_us.count() - process_time;
    while(true) {
		auto start = std::chrono::high_resolution_clock::now();
		while(std::chrono::duration_cast<spacetime_t>(std::chrono::high_resolution_clock::now() - start).count() < process_time);
        std::this_thread::sleep_for(spacetime_t(sleep_time));
    }
}

void print_thread_args(const thread_args& arg) {
    // formated print & align thread_args inline
    printf("cpu_id: %2d, priority: %2d, rate: %5.2f%%\n", arg.cpu_id == 0x3f ? -1 : arg.cpu_id, arg.priority, arg.rate / g_rate_base * 100);
}

void help() {
    std::cout << g_helper_str << std::endl;
    exit(0);
}

template<typename ...ARGS>
void invalid_usage(const char* fmt, ARGS... args) {
    std::cout << "Invalid usage: ";
    printf(fmt, args...);
    std::cout << std::endl;
    std::cout << g_helper_str << std::endl;
    exit(1);
}

template<typename ...ARGS>
void check(bool cond, const char* fmt, ARGS... args) {
    if (!cond) {
        invalid_usage(fmt, args...);
    }
}

bool parse_from_cpuid_prioriy_rate(thread_args& arg, const char* str) {
    int cpu_id = 0x3f;
    float rate = 0.0;
    int priority = 0;
    if (sscanf(str, "%d,%d,%f", &cpu_id, &priority, &rate) != 3) {
        return false;
    }
    check(cpu_id >= -1 && cpu_id < g_hardware_threads, "Invalid cpu_id: %d", cpu_id);
    check(priority >= 0 && priority <= 99, "Invalid priority: %d", priority);
    check(rate >= 0.0 && rate <= 1.0, "Invalid rate: %f", rate);

    arg.cpu_id = cpu_id;
    arg.rate = rate * g_rate_base;
    arg.priority = priority;
    arg.specific = 1;
    return true;
}

bool parse_from_cpuid_rate(thread_args& arg, const char* str) {
    int cpu_id = 0x3f;
    float rate = 0.0;
    if (sscanf(str, "%d,%f", &cpu_id, &rate) != 2) {
        return false;
    }
    check(cpu_id >= -1 && cpu_id < g_hardware_threads, "Invalid cpu_id: %d", cpu_id);
    check(rate >= 0.0 && rate <= 1.0, "Invalid rate: %f", rate);

    arg.cpu_id = cpu_id;
    arg.rate = rate * g_rate_base;
    arg.specific = 1;
    return true;
}

bool parse_from_rate(thread_args& arg, const char* str) {
    float rate = 0.0;
    if (sscanf(str, "%f", &rate) != 1) {
        return false;
    }
    check(rate >= 0.0 && rate <= 1.0, "Invalid rate: %f", rate);

    arg.rate = rate * g_rate_base;
    arg.specific = 1;
    return true;
}

std::unordered_map<char, cmd_job_t> g_cmd_jobs = {
    {'f', [](std::vector<thread_args>& args, thread_args& arg) {
        for (auto i = 0; i < args.size(); ++i) {
            args[i] = arg;
        }
    }},
    {'F', [](std::vector<thread_args>& args, thread_args& arg) {
        for (auto i = 0; i < args.size(); ++i) {
            args[i] = arg;
            args[i].cpu_id = i;
        }
    }},
    {'r', [](std::vector<thread_args>& args, thread_args& arg) {
        for (auto i = 0; i < args.size(); ++i) {
            if (!args[i].valid) {
                args[i] = arg;
            }
        }
    }},
    {'R', [](std::vector<thread_args>& args, thread_args& arg) {
        for (auto i = 0; i < args.size(); ++i) {
            if (!args[i].valid) {
                args[i] = arg;
                args[i].cpu_id = i;
            }
        }
    }},
};

bool parse_from_cmd_rate(std::vector<thread_args>& args, thread_args& arg, const char* str) {
    char cmd = 0;
    float rate = 0.0;
    int priority = 0;
    if (sscanf(str, "%c,%d,%f", &cmd, &priority, &rate) == 3) {
        check(g_cmd_jobs.find(cmd) != g_cmd_jobs.end(), "Invalid cmd: %c", cmd);
        check(priority >= 0 && priority <= 99, "Invalid priority: %d", priority);
        check(rate >= 0.0 && rate <= 1.0, "Invalid rate: %f", rate);

        arg.rate = rate * g_rate_base;
        arg.priority = priority;
        g_cmd_jobs[cmd](args, arg);

        return true;
    }
    if (sscanf(str, "%c,%f", &cmd, &rate) == 2) {
        check(g_cmd_jobs.find(cmd) != g_cmd_jobs.end(), "Invalid cmd: %c", cmd);
        check(rate >= 0.0 && rate <= 1.0, "Invalid rate: %f", rate);

        arg.rate = rate * g_rate_base;
        g_cmd_jobs[cmd](args, arg);

        return true;
    }

    return false;
}

std::vector<thread_args> parse_args(int argc, char** argv) {
    // check argc, argc should less than hardware threads
    check(argc <= g_hardware_threads + 1, "Too many arguments, max %d arguments allowed", g_hardware_threads);
    std::vector<thread_args> args(g_hardware_threads, thread_args());
    for (auto i = 1; i < argc; ++i) {
        thread_args arg;
        arg.valid = true;

        bool parse_ok = parse_from_cpuid_prioriy_rate(arg, argv[i]) ||
                        parse_from_cpuid_rate(arg, argv[i]) ||
                        parse_from_rate(arg, argv[i]);
        if (!parse_ok && parse_from_cmd_rate(args, arg, argv[i])) {
            continue;
        }
        check(parse_ok, "Invalid argument: %s", argv[i]);

        int args_idx = arg.cpu_id == -1 ? i - 1 : arg.cpu_id;
        args[args_idx] = arg;
    }
    return args;
}

void startup(std::vector<thread_args>& args) {
    std::vector<std::thread> threads;
    for (auto& arg : args) {
        if (!arg.valid) {
            continue;
        }

        print_thread_args(arg);
        threads.emplace_back(process, arg.rate / g_rate_base);
        if (arg.cpu_id < 0x3f) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(arg.cpu_id, &cpuset);
            pthread_setaffinity_np(threads.back().native_handle(), sizeof(cpu_set_t), &cpuset);
        }
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

int main(int argc, char** argv) {
    auto args = parse_args(argc, argv);
    startup(args);

    help();
	return 0;
}
