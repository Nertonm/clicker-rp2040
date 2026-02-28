#ifndef SERVICE_DISC_H
#define SERVICE_DISC_H

#include "lwip/ip_addr.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Tenta descobrir o servidor via UDP Broadcast.
 * Retorna true se encontrado, preenchendo out_ip e out_port.
 */
bool service_disc_discover(ip_addr_t *out_ip, uint16_t *out_port,
                           absolute_time_t timeout_deadline);

#endif // SERVICE_DISC_H
