/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/logging/log.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/net/openthread.h>
#include <openthread/coap.h>
#include <openthread/ip6.h>
#include <openthread/message.h>
#include <openthread/thread.h>

#include "ot_coap_utils.h"

LOG_MODULE_REGISTER(ot_coap_utils, CONFIG_OT_COAP_UTILS_LOG_LEVEL);

struct server_context {
	struct otInstance *ot;
	bool pump_active;
	light_request_callback_t on_light_request;
	light_request_callback_t on_temperature_request;
};

static struct server_context srv_context = {
	.ot = NULL,
	.pump_active = false,
	.on_light_request = NULL,
	.on_temperature_request = NULL,
};

void coap_activate_pump(void)
{
	srv_context.pump_active = true;
}

bool coap_is_pump_active(void)
{
	return srv_context.pump_active;
}

void coap_diactivate_pump(void)
{
	srv_context.pump_active = false;
}

/**@brief Definition of CoAP resources for light. */
static otCoapResource light_resource = {
	.mUriPath = LIGHT_URI_PATH,
	.mHandler = NULL,
	.mContext = NULL,
	.mNext = NULL,
};

/**@brief Definition of CoAP resources for temperature. */
static otCoapResource temperature_resource = {
	.mUriPath = TEMPERATURE_URI_PATH,
	.mHandler = NULL,
	.mContext = NULL,
	.mNext = NULL,
};

static otError temperature_response_send(otMessage *request_message, const otMessageInfo *message_info)
{
	otError error = OT_ERROR_NO_BUFS;
	otMessage *response;
	const void *payload;
	uint16_t payload_size;
	static uint8_t val = 23;
	static int8_t factor = 1;

	// simulate data
	if (val > 39)
		factor *= -1;
	else if (val < 16)
		factor *= -1;
	val += factor;

	response = otCoapNewMessage(srv_context.ot, NULL);
	if (response == NULL) {
		goto end;
	}

	otCoapMessageInit(response, OT_COAP_TYPE_NON_CONFIRMABLE,
			  OT_COAP_CODE_CONTENT);

	error = otCoapMessageSetToken(
		response, otCoapMessageGetToken(request_message),
		otCoapMessageGetTokenLength(request_message));
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapMessageSetPayloadMarker(response);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	payload = &val;
	payload_size = sizeof(val);

	error = otMessageAppend(response, payload, payload_size);
	if (error != OT_ERROR_NONE) {
		goto end;
	}

	error = otCoapSendResponse(srv_context.ot, response, message_info);

	LOG_INF("Temperature response sent: %d degC", val);

end:
	if (error != OT_ERROR_NONE && response != NULL) {
		otMessageFree(response);
	}

	return error;
}

static otError light_response_send(otMessage *request_message, const otMessageInfo *message_info)
{
	otError error = OT_ERROR_NO_BUFS;
	otMessage *response;
	const void *payload;
	uint16_t payload_size;
	uint8_t light_status;

	// create response message
	response = otCoapNewMessage(srv_context.ot, NULL);
	if (response == NULL) {
		LOG_INF("Error in otCoapNewMessage()");
		goto end;
	}

	// init response message
	otCoapMessageInit(response, OT_COAP_TYPE_NON_CONFIRMABLE,
			  OT_COAP_CODE_CONTENT);

	// set message token
	error = otCoapMessageSetToken(
		response, otCoapMessageGetToken(request_message),
		otCoapMessageGetTokenLength(request_message));
	if (error != OT_ERROR_NONE) {
		LOG_INF("Error in otCoapMessageSetToken()");
		goto end;
	}

	// set message payload marker
	error = otCoapMessageSetPayloadMarker(response);
	if (error != OT_ERROR_NONE) {
		LOG_INF("Error in otCoapMessageSetPayloadMarker()");
		goto end;
	}

	// update payload
	if (coap_is_pump_active())
		light_status = 1;
	else
		light_status - 0;
	payload = &light_status;
	payload_size = sizeof(light_status);

	error = otMessageAppend(response, payload, payload_size);
	if (error != OT_ERROR_NONE) {
		LOG_INF("Error in otMessageAppend()");
		goto end;
	}

	error = otCoapSendResponse(srv_context.ot, response, message_info);
	if (error != OT_ERROR_NONE) {
		LOG_INF("Error in otCoapSendResponse()");
		goto end;
	}

	LOG_INF("Light response sent: %d", light_status);
	
end:
	if (error != OT_ERROR_NONE && response != NULL) {
		LOG_INF("Couldn't send Light response");
		otMessageFree(response);
	}

	return error;
}

static void light_request_handler(void *context, otMessage *message, const otMessageInfo *message_info)
{
	uint8_t command;
	otMessageInfo msg_info;

	ARG_UNUSED(context);

	if (otCoapMessageGetType(message) != OT_COAP_TYPE_NON_CONFIRMABLE) {
		LOG_ERR("Light handler - Unexpected type of message");
		goto end;
	}

	if (otCoapMessageGetCode(message) != OT_COAP_CODE_PUT) {
		LOG_ERR("Light handler - Unexpected CoAP code");
		goto end;
	}

	if (otMessageRead(message, otMessageGetOffset(message), &command, 1) !=
	    1) {
		LOG_ERR("Light handler - Missing light command");
		goto end;
	}

	msg_info = *message_info;
	memset(&msg_info.mSockAddr, 0, sizeof(msg_info.mSockAddr));

	LOG_INF("Received light request: %c", command);

	srv_context.on_light_request(command); // update light in coap_server.c

	light_response_send(message, &msg_info);

end:
	return;
}

static void temperature_request_handler(void *context, otMessage *message,
				  const otMessageInfo *message_info)
{
	otError error;
	otMessageInfo msg_info;

	ARG_UNUSED(context);

	LOG_INF("Received temperature request");

	if ((otCoapMessageGetType(message) == OT_COAP_TYPE_NON_CONFIRMABLE) &&
	    (otCoapMessageGetCode(message) == OT_COAP_CODE_GET)) {
		msg_info = *message_info;
		memset(&msg_info.mSockAddr, 0, sizeof(msg_info.mSockAddr));

		// the on_temperature_request function is not implemented yet. In the future, this is where the ADC value will be read (from coap_server.c)
		srv_context.on_temperature_request;

		temperature_response_send(message, &msg_info);
	}
	else
	{
		LOG_INF("Bad temperature request type or code.");
	}
}
 
static void coap_default_handler(void *context, otMessage *message,
				 const otMessageInfo *message_info)
{
	ARG_UNUSED(context);
	ARG_UNUSED(message);
	ARG_UNUSED(message_info);

	LOG_INF("Received CoAP message that does not match any request "
		"or resource");
}


int ot_coap_init(light_request_callback_t on_light_request)
{
	otError error;
	srv_context.on_light_request = on_light_request;

	srv_context.ot = openthread_get_default_instance();
	if (!srv_context.ot) {
		LOG_ERR("There is no valid OpenThread instance");
		error = OT_ERROR_FAILED;
		goto end;
	}

	light_resource.mContext = srv_context.ot;
	light_resource.mHandler = light_request_handler;

	temperature_resource.mContext = srv_context.ot;
	temperature_resource.mHandler = temperature_request_handler;

	otCoapSetDefaultHandler(srv_context.ot, coap_default_handler, NULL);
	otCoapAddResource(srv_context.ot, &light_resource);
	otCoapAddResource(srv_context.ot, &temperature_resource);

	error = otCoapStart(srv_context.ot, COAP_PORT);
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to start OT CoAP. Error: %d", error);
		goto end;
	}
	LOG_INF("Coap Server has started");

end:
	return error == OT_ERROR_NONE ? 0 : 1;
}
