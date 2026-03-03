/**
 * @file stress_test.h
 * @author
 * @date 2026-03-01
 * @brief Interface para os testes de estresse do firmware.
 */

#ifndef STRESS_TEST_H
#define STRESS_TEST_H

/**
 * @brief Inicia as tarefas de teste de estresse.
 *
 * @note Esta função só executa lógica real se a macro STRESS_TEST estiver definida.
 */
void stress_test_start(void);

#endif // STRESS_TEST_H
