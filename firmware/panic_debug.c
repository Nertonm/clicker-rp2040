#include <stdio.h>
#include <stdarg.h>
#include "pico/stdlib.h"
#include <unistd.h>

// Debug panic handler: print the panic message and the return address
void __attribute__((noreturn)) pico_panic_debug(const char *fmt, ...) {
  puts("\n*** PANIC (debug) ***\n");
  if (fmt) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    puts("\n");
  }

  void *ret0 = __builtin_return_address(0);
  void *ret1 = __builtin_return_address(1);
  printf("[PANIC] caller0=%p caller1=%p\n", ret0, ret1);

  _exit(1);
}
