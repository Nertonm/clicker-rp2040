#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdio.h>
#include "pico.h"

#ifdef __cplusplus
extern "C" {
#endif

#define configUSE_PREEMPTION                    1
#define configCPU_CLOCK_HZ                      133000000
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ                      1000
#endif
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                256
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_TIME_SLICING                  1
#define configUSE_TICK_HOOK                     0

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                16
#define configTIMER_TASK_STACK_DEPTH            512

#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#define configTOTAL_HEAP_SIZE                   (80 * 1024)

#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8

#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_IDLE_HOOK                     1

#define configNUMBER_OF_CORES                   1
#define configRUN_MULTIPLE_PRIORITIES           1
#define configUSE_CORE_AFFINITY                 0

#define configLWIP_TASK_PRIORITY                (configMAX_PRIORITIES - 2)
#define configLWIP_TASK_STACK_DEPTH             512

#define configSUPPORT_PICO_SYNC_INTEROP         1
#define configSUPPORT_PICO_TIME_INTEROP         1

void vAssertCalled(const char *file, int line);
#define configASSERT(x)                         do { if ((x) == 0) vAssertCalled(__FILE__, __LINE__); } while (0)

#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xSemaphoreGetMutexHolder        1

#ifdef __cplusplus
}
#endif

#endif // FREERTOS_CONFIG_H
