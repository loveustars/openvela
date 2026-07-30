#include <errno.h>

/*
 * Some OpenVela/Newlib build combinations pull in the reentrant signal
 * wrapper from the toolchain libc, which expects a low-level _getpid().
 * This app never relies on real process IDs, so a weak stub is enough
 * to satisfy the linker without changing board code.
 */
__attribute__((weak)) int _getpid(void)
{
    return 1;
}

__attribute__((weak)) int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = ENOSYS;
    return -1;
}
