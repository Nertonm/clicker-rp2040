/**
 * @file panic_debug.c
 * @author
 * @date 2026-03-01
 * @brief Tratador de pânico customizado para depuração de hardware.
 *
 * Provê informações detalhadas sobre falhas de baixo nível, incluindo
 * rastreamento (backtrace) simplificado via endereços de retorno.
 */

#include <stdio.h>
#include <stdarg.h>
#include "pico/stdlib.h"
#include <unistd.h>

/**
 * @brief Tratador de pânico que imprime mensagem e endereços de retorno dos chamadores.
 *
 * @param[in] fmt String de formato da mensagem (estilo printf).
 * @param[in] ... Argumentos variáveis para a mensagem.
 */
void __attribute__((noreturn)) pico_panic_debug(const char *fmt, ...) {
  puts("\n*** PANIC (debug) ***\n");
  if (fmt) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    puts("\n");
  }

  /* Obtém os endereços de retorno para ajudar na localização do erro */
  void *ret0 = __builtin_return_address(0);
  void *ret1 = __builtin_return_address(1);
  printf("[PANIC] caller0=%p caller1=%p\n", ret0, ret1);

  /* Finaliza o processo/firmware */
  _exit(1);
}
