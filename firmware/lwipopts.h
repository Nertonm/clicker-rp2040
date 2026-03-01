/**
 * @file lwipopts.h
 * @brief Configurações da pilha de rede lwIP para o RP2040.
 *
 * Define parâmetros de performance, depuração e funcionalidades ativadas
 * para o driver de rede CYW43439.
 */

#ifndef _LWIPOPTS_EXAMPLE_COMMONH_H
#define _LWIPOPTS_EXAMPLE_COMMONH_H

/** @name Configurações de Sistema */
/** @{ */
#ifndef NO_SYS
#define NO_SYS 0 /**< Utiliza o kernel FreeRTOS. */
#endif
#ifndef LWIP_SOCKET
#define LWIP_SOCKET 1 /**< Habilita API de Sockets. */
#endif

/* Evita conflito de struct timeval com newlib (sys/_timeval.h) */
#define LWIP_TIMEVAL_PRIVATE 0
/** @} */

/** @name Gerenciamento de Memória */
/** @{ */
#define MEM_ALIGNMENT 4
#ifndef MEM_SIZE
#define MEM_SIZE 4000
#endif
#define PBUF_POOL_SIZE 24
/** @} */

/** @name Protocolos Habilitados */
/** @{ */
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_DHCP 1
#define LWIP_IPV4 1
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DNS 1
/** @} */

/** @name Configurações TCP */
/** @{ */
#define TCP_WND (8 * TCP_MSS)
#define TCP_MSS 1460
#define TCP_SND_BUF (8 * TCP_MSS)

/* Garante que o pool de segmentos TCP nunca seja menor que a fila de envio */
#define MEMP_NUM_TCP_SEG TCP_SND_QUEUELEN
/** @} */

#ifndef NDEBUG
#define LWIP_DEBUG 1
#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 1
#endif

#endif /* __LWIPOPTS_H__ */
