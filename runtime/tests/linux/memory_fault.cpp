#include "posix_memory_fault.h"

#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>

namespace {
void* page;
bool expectedWrite;
volatile sig_atomic_t handled = 0;

bool Resolve(void* address, bool isWrite) {
    if (address != page || isWrite != expectedWrite) return false;
    handled = 1;
    return mprotect(page, 4096, PROT_READ | PROT_WRITE) == 0;
}
}  // namespace

int main() {
    RuntimeSignals::InstallMemoryFaultHandler(Resolve);
    page = mmap(nullptr, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) return 1;
    expectedWrite = true;
    *static_cast<volatile unsigned char*>(page) = 42;
    if (!handled) return 2;
    handled = 0;
    expectedWrite = false;
    if (mprotect(page, 4096, PROT_NONE) != 0) return 3;
    if (*static_cast<volatile unsigned char*>(page) != 42 || !handled) return 4;
    munmap(page, 4096);

    const pid_t child = fork();
    if (child < 0) return 5;
    if (child == 0) {
        rlimit limit{0, 0};
        setrlimit(RLIMIT_CORE, &limit);
        raise(SIGSEGV);
        _exit(99);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 6;
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV ? 0 : 7;
}
