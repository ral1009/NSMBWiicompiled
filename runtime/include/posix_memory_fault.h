#pragma once

#if !defined(_WIN32)
#include <csignal>
#include <stdexcept>
#include <ucontext.h>
#include <unistd.h>

namespace RuntimeSignals {

inline bool (*memoryFaultResolver)(void*, bool) = nullptr;

inline void MemoryFault(int signal, siginfo_t* info, void* context) {
    bool isWrite = false;
#if defined(__x86_64__)
    if (context) {
        isWrite = (static_cast<ucontext_t*>(context)->uc_mcontext.gregs[REG_ERR] & 2) != 0;
    }
#endif
    if (info && memoryFaultResolver && memoryFaultResolver(info->si_addr, isWrite)) return;
    constexpr char message[] = "[nsmbw] Unhandled native memory fault.\n";
    (void)write(STDERR_FILENO, message, sizeof(message) - 1);
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

inline void InstallMemoryFaultHandler(bool (*resolver)(void*, bool)) {
    memoryFaultResolver = resolver;
    struct sigaction action{};
    action.sa_sigaction = MemoryFault;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGSEGV, &action, nullptr) != 0 || sigaction(SIGBUS, &action, nullptr) != 0) {
        throw std::runtime_error("Could not install the guest memory fault handler.");
    }
}

}  // namespace RuntimeSignals
#endif
