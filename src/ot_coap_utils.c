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
	temperature_request_callback_t on_temperature_request;
	info_request_callback_t on_info_request;
};

static struct server_context srv_context = {
	.ot = NULL,
	.pump_active = false,
	.on_light_request = NULL,
	.on_temperature_request = NULL,
};

struct fw_version {
	// FW version
	const char * fw_version_buf;
	uint8_t fw_version_size;
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
/* Information resource callbacks*/
static otError info_response_send(otMessage *request_message, const otMessageInfo *message_info)
{
	otError error = OT_ERROR_NO_BUFS;
	otMessage *response;
	const void *payload;
	uint16_t payload_size;
	struct fw_version fw;

	fw = srv_context.on_info_request(); // get temperature from coap_server.c

	// create response message
	response = otCoapNewMessage(srv_context.ot, NULL);
	if (response == NULL) {
		LOG_INF("Error in otCoapNewMessage()");
		goto end;
	}

	// init response message
	otCoapMessageInitResponse(response, request_message, OT_COAP_TYPE_ACKNOWLEDGMENT,
			  OT_COAP_CODE_CHANGED);

	// set message payload marker
	error = otCoapMessageSetPayloadMarker(response);
	if (error != OT_ERROR_NONE) {
		LOG_INF("Error in otCoapMessageSetPayloadMarker()");
		goto end;
	}

	payload = fw.fw_version_buf;
	payload_size = fw.fw_version_size;

	error = otMessageAppend(response, payload, payload_size);
	if (error != OT_ERROR_NONE) {
		LOG_INF("Error in otMessageAppend()");
		goto end;
	}

	LOG_INF("Firmware version is: %s", fw.fw_version_buf);

end:
	if (error != OT_ERROR_NONE && response != NULL) {
		otMessageFree(response);
	}

	return error;
}
static void info_request_handler(void *context, otMessage *message, const otMessageInfo *message_info)
{
	otError error;
	otMessageInfo msg_info;

	ARG_UNUSED(context);

	LOG_INF("Received info request");

	if ((otCoapMessageGetType(message) == OT_COAP_TYPE_CONFIRMABLE) &&
	    (otCoapMessageGetCode(message) == OT_COAP_CODE_GET)) {
		msg_info = *message_info;
		memset(&msg_info.mSockAddr, 0, sizeof(msg_info.mSockAddr));

		info_response_send(message, &msg_info);
	}
	else
	{
		LOG_INF("Bad info request type or code.");
	}
}

/* Temperature resource callbacks*/
static otError temperature_response_send(otMessage *request_message, const otMessageInfo *message_info)
{
	otError error = OT_ERROR_NO_BUFS;
	otMessage *response;
	const void *payload;
	uint16_t payload_size;
	int8_t val = 0;

	val = srv_context.on_temperature_request(); // get temperature from coap_server.c
	
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
static void temperature_request_handler(void *context, otMessage *message, const otMessageInfo *message_info)
{
	otError error;
	otMessageInfo msg_info;

	ARG_UNUSED(context);

	LOG_INF("Received temperature request");

	if ((otCoapMessageGetType(message) == OT_COAP_TYPE_NON_CONFIRMABLE) &&
	    (otCoapMessageGetCode(message) == OT_COAP_CODE_GET)) {
		msg_info = *message_info;
		memset(&msg_info.mSockAddr, 0, sizeof(msg_info.mSockAddr));

		temperature_response_send(message, &msg_info);
	}
	else
	{
		LOG_INF("Bad temperature request type or code.");
	}
}
/* Light resource callbacks*/
static otError light_put_response_send(otMessage *request_message, const otMessageInfo *message_info)
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
	otCoapMessageInitResponse(response, request_message, OT_COAP_TYPE_ACKNOWLEDGMENT,
			  OT_COAP_CODE_CHANGED);

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

	LOG_INF("Light PUT response sent: %d", light_status);
	
end:
	if (error != OT_ERROR_NONE && response != NULL) {
		LOG_INF("Couldn't send Light response");
		otMessageFree(response);
	}

	return error;
}
static otError light_get_response_send(otMessage *request_message, const otMessageInfo *message_info)
{
	otError error = OT_ERROR_NO_BUFS;
	otMessage *response;
	const void *payload;
	uint16_t payload_size;
	uint8_t val = coap_is_pump_active();
	
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
		LOG_INF("Error in otCoapMessageSetToken()");
		goto end;
	}

	error = otCoapMessageSetPayloadMarker(response);
	if (error != OT_ERROR_NONE) {
		LOG_INF("Error in otCoapMessageSetPayloadMarker()");
		goto end;
	}

	payload = &val;
	payload_size = sizeof(val);

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

	LOG_INF("Light GET response sent: %d", val);

end:
	if (error != OT_ERROR_NONE && response != NULL) {
		otMessageFree(response);
		LOG_INF("Couldn't send Light GET response");
	}

	return error;
}
static void light_request_handler(void *context, otMessage *message, const otMessageInfo *message_info)
{
	uint8_t command;
	otMessageInfo msg_info;

	uint8_t isTypePut = 0;

	ARG_UNUSED(context);

	if ((otCoapMessageGetType(message) == OT_COAP_TYPE_CONFIRMABLE) && (otCoapMessageGetCode(message) == OT_COAP_CODE_PUT))
	{
		isTypePut = 1;
	}
	else if ((otCoapMessageGetType(message) == OT_COAP_TYPE_NON_CONFIRMABLE) && (otCoapMessageGetCode(message) == OT_COAP_CODE_GET))
	{
		isTypePut = 0;
	}
	else
	{
		LOG_INF("Bad light request type/code.");
		goto end;
	}

	msg_info = *message_info;
	memset(&msg_info.mSockAddr, 0, sizeof(msg_info.mSockAddr));

	if (isTypePut) {
		if (otMessageRead(message, otMessageGetOffset(message), &command, 1) != 1) {
			LOG_ERR("Light handler - Missing light command");
			goto end;
		}
		srv_context.on_light_request(command); // update light in coap_server.c
		LOG_INF("Received light PUT request: %c", command);
		light_put_response_send(message, &msg_info);
	}
	else {
		LOG_INF("Received light GET request");
		light_get_response_send(message, &msg_info);
	}

end:
	return;
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


int ot_coap_init(light_request_callback_t on_light_request, temperature_request_callback_t on_temperature_request, info_request_callback_t on_info_request)
{
	otError error;
	srv_context.on_light_request = on_light_request;
	srv_context.on_temperature_request = on_temperature_request;
	srv_context.on_info_request = on_info_request;

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
