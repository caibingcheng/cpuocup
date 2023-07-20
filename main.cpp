#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>

#include <cstring>
#include <cstdio>
#include <pthread.h>
#include <unistd.h>

#define MAJOR_VERSION 0
#define MINOR_VERSION 1
#define PATCH_VERSION 0
// version string in macro, compatible with std::string
#define VERSION_STRING "v" + std::to_string(MAJOR_VERSION) + "." + std::to_string(MINOR_VERSION) + "." + std::to_string(PATCH_VERSION)

struct thread_args {
    int8_t cpu_id = -1;
    int8_t priority = -1;
    uint16_t rate : 14;
    uint16_t valid : 1;
    uint16_t specific : 1;
};

using spacetime_t = std::chrono::microseconds;
using cmd_job_t = std::function<void(std::vector<thread_args>&, thread_args&)>;

constexpr float g_rate_base = 10000.0f;
constexpr spacetime_t g_interval_us = spacetime_t(10000);
int g_hardware_threads = std::thread::hardware_concurrency();
std::string g_helper_str =
// unix style helper
"\033[1mNAME\033[0m\n"
"    cpuocup - set cpu userspace usage rate\n"
"\033[1mVERSION\033[0m\n"
"    " + std::string(VERSION_STRING) + "\n"
"\033[1mUSAGE\033[0m\n"
// can set [rate] [cpu_id,rate] [cpu_id,priority,rate] [cmd,rate] [cmd,priority,rate]
"    \033[1mcpuocup\033[0m [\033[4mrate\033[0m] [\033[4mcpu_id\033[0m,\033[4mrate\033[0m] [\033[4mcpu_id\033[0m,\033[4mpriority\033[0m,\033[4mrate\033[0m] [\033[4mcmd\033[0m,\033[4mrate\033[0m] [\033[4mcmd\033[0m,\033[4mpriority\033[0m,\033[4mrate\033[0m] ...\n"
"\033[1mDESCRIPTION\033[0m\n"
"    This program is used to set cpu rate. Max " + std::to_string(g_hardware_threads) + " threads are supported at the device.\n"
"    \033[1mrate\033[0m: thread rate, 0.0 <= rate <= 1.0\n"
"    \033[1mcpu_id\033[0m: cpu id, -1 means thread not bind any cpu, range: [-1, " + std::to_string(g_hardware_threads-1) + "]\n"
"    \033[1mpriority\033[0m: thread priority, range: [0, 99]\n"
// cmd support: f, F, r, R
"    \033[1mcmd\033[0m: f, r, F, R\n"
"        f: set to all threads\n"
"        r: set to all threads which is not specific\n"
"        F: set to all threads, and bind to corresponding cpu\n"
"        R: set to all threads which is not specific, and bind to corresponding cpu\n"
"\033[1mEXAMPLE\033[0m\n"
// give 5 classic examples of each usage, and explain the meaning of each example
"    \033[1mcpuocup\033[0m 0.5 0.9\n"
"        set thread 0 to 50% usage, and thread 1 to 90% usage\n"
"    \033[1mcpuocup\033[0m 1,0.5\n"
"        set thread 1 to 50% usage, and bind to cpu 1\n"
"    \033[1mcpuocup\033[0m 1,20,0.5\n"
"        set thread 1 to 50% usage, and bind to cpu 1, and set thread priority to 20\n"
"    \033[1mcpuocup\033[0m f,0.5\n"
"        set all threads to 50% usage\n"
"    \033[1mcpuocup\033[0m 1,20,0.5, r,40,0.9\n"
"        set thread 1 to 50% usage, and bind to cpu 1, and set thread priority to 20\n"
"        set all threads which is not specific to 90% usage, and set thread priority to 40, and bind to corresponding cpus\n"
"\033[1mAUTHOR\033[0m\n"
"    Written by \033[1mcaibingcheng\033[0m.\n"
"\033[1mREPORTING BUGS\033[0m\n"
"    Report bugs to \033[1mjack_cbc@163.com\033[0m.\n"
"\033[1mCOPYRIGHT\033[0m\n"
"    This is free software: you are free to change and redistribute it.\n"
"    There is NO WARRANTY, to the extent permitted by law.\n"
;

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
    printf("cpu_id: %2d, priority: %2d, rate: %5.2f%%\n", arg.cpu_id, arg.priority, arg.rate / g_rate_base * 100);
}

void help() {
    std::cout << g_helper_str << std::endl;
    exit(0);
}

template<typename ...ARGS>
void invalid_usage(const char* fmt, ARGS... args) {
    // highlight invalid usage
    printf("\033[1m");
    printf(fmt, args...);
    printf("\033[0m\n\n");
    help();
    exit(1);
}

template<typename ...ARGS>
void check(bool cond, const char* str) {
    if (!cond) {
        invalid_usage("%s", str);
    }
}

template<typename ...ARGS>
void check(bool cond, const char* fmt, ARGS... args) {
    if (!cond) {
        invalid_usage(fmt, args...);
    }
}

bool parse_from_cpuid_prioriy_rate(thread_args& arg, const char* str) {
    int cpu_id = -1;
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
    int cpu_id = -1;
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
            if (!args[i].specific) {
                args[i] = arg;
            }
        }
    }},
    {'R', [](std::vector<thread_args>& args, thread_args& arg) {
        for (auto i = 0; i < args.size(); ++i) {
            if (!args[i].specific) {
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
    // process -h, --help first
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        help();
        exit(0);
    }

    // check argc, argc should less than hardware threads
    check(argc <= g_hardware_threads + 1, "Too many arguments, max %d arguments allowed", g_hardware_threads);
    std::vector<thread_args> args(g_hardware_threads, thread_args());
    for (auto i = 1; i < argc; ++i) {
        thread_args arg;
        arg.valid = 1;

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

        threads.emplace_back(process, arg.rate / g_rate_base);
        if (arg.cpu_id >= 0) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(arg.cpu_id, &cpuset);
            pthread_setaffinity_np(threads.back().native_handle(), sizeof(cpu_set_t), &cpuset);

            // check if cpu affinity set success
            cpu_set_t get;
            CPU_ZERO(&get);
            pthread_getaffinity_np(threads.back().native_handle(), sizeof(cpu_set_t), &get);
            check(CPU_ISSET(arg.cpu_id, &get), "Set cpu affinity failed, %s", strerror(errno));
        }
        if (arg.priority >= 0) {
            struct sched_param param;
            param.sched_priority = arg.priority;
            pthread_setschedparam(threads.back().native_handle(), SCHED_FIFO, &param);

            // check if priority set success
            int policy;
            pthread_getschedparam(threads.back().native_handle(), &policy, &param);
            check(param.sched_priority == arg.priority, "Set priority failed, %s", strerror(errno));
        }

        print_thread_args(arg);
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

int main(int argc, char** argv) {
    auto args = parse_args(argc, argv);
    startup(args);

	return 0;
}
