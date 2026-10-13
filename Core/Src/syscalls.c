/* Minimal syscalls for newlib-nano. */
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

extern int _end;
static char *heap_end;

caddr_t _sbrk(int incr)
{
  extern char _estack;
  char *prev_heap_end;

  if (heap_end == 0)
  {
    heap_end = (char *)&_end;
  }

  prev_heap_end = heap_end;
  if ((heap_end + incr) > (&_estack))
  {
    errno = ENOMEM;
    return (caddr_t)-1;
  }

  heap_end += incr;
  return (caddr_t)prev_heap_end;
}

int _close(int file) { (void)file; return -1; }
int _fstat(int file, struct stat *st) { (void)file; st->st_mode = S_IFCHR; return 0; }
int _isatty(int file) { (void)file; return 1; }
int _lseek(int file, int ptr, int dir) { (void)file; (void)ptr; (void)dir; return 0; }
int _read(int file, char *ptr, int len) { (void)file; (void)ptr; (void)len; return 0; }
int _write(int file, char *ptr, int len) { (void)file; (void)ptr; return len; }
void _exit(int status) { (void)status; while (1) {} }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
void _init(void) {}
void _fini(void) {}
