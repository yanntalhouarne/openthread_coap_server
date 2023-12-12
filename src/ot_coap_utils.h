/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __OT_COAP_UTILS_H__
#define __OT_COAP_UTILS_H__

#include <coap_server_client_interface.h>

/**@brief Type definition of the function used to handle light resource change.
 */
typedef void (*light_request_callback_t)(uint8_t cmd);
typedef int8_t * (*temperature_request_callback_t)();
typedef struct fw_version (*info_request_callback_t)();
int ot_coap_init(light_request_callback_t on_light_request, temperature_request_callback_t, info_request_callback_t);


#endif
