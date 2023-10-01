/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

//#include <stdio. h> // for snprintf
#include <zephyr/kernel.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/openthread.h>
#include <openthread/thread.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <openthread/srp_client.h>
#include <openthread/srp_client_buffers.h>
#include <zephyr/random/rand32.h>

#include "ot_coap_utils.h"
#include "ot_srp_config.h"

LOG_MODULE_REGISTER(coap_server, CONFIG_COAP_SERVER_LOG_LEVEL);

#define OT_CONNECTION_LED DK_LED1
#define PROVISIONING_LED DK_LED3
#define LIGHT_LED DK_LED4

static struct k_work provisioning_work;

static struct k_timer led_timer;
static struct k_timer provisioning_timer;

const char hostname[] = SRP_CLIENT_HOSTNAME;
char realhostname[sizeof(hostname)+SRP_CLIENT_RAND_SIZE] = {0};
const char service_instance[] = SRP_CLIENT_SERVICE_INSTANCE;
char realinstance[sizeof(service_instance)+SRP_CLIENT_RAND_SIZE] = {0};
const char service_name[] = "_ot._udp";

static void on_light_request(uint8_t command)
{
	static uint8_t val;

	switch (command) {
	case THREAD_COAP_UTILS_LIGHT_CMD_ON:
		dk_set_led_on(LIGHT_LED);
		val = 1;
		break;

	case THREAD_COAP_UTILS_LIGHT_CMD_OFF:
		dk_set_led_off(LIGHT_LED);
		val = 0;
		break;

	case THREAD_COAP_UTILS_LIGHT_CMD_TOGGLE:
		val = !val;
		dk_set_led(LIGHT_LED, val);
		break;

	default:
		break;
	}
}

static void on_button_changed(uint32_t button_state, uint32_t has_changed)
{
	uint32_t buttons = button_state & has_changed;

	if (buttons & DK_BTN4_MSK) {
		//k_work_submit(&provisioning_work);
	}
}

void on_srp_client_updated(otError aError, const otSrpClientHostInfo *aHostInfo, const otSrpClientService *aServices, const otSrpClientService *aRemovedServices, void *aContext);

void on_srp_client_updated(otError aError, const otSrpClientHostInfo *aHostInfo, const otSrpClientService *aServices, const otSrpClientService *aRemovedServices, void *aContext)
{

	LOG_INF("SRP callback: ");
	printk(otThreadErrorToString(aError));
}

static void on_thread_state_changed(otChangedFlags flags, struct openthread_context *ot_context,
				    void *user_data)
{
	static uint8_t oneTime = 0;
	if (flags & OT_CHANGED_THREAD_ROLE) {
		switch (otThreadGetDeviceRole(ot_context->instance)) {
		case OT_DEVICE_ROLE_CHILD:
		case OT_DEVICE_ROLE_ROUTER:
		case OT_DEVICE_ROLE_LEADER:
			dk_set_led_on(OT_CONNECTION_LED);
			otSrpClientBuffersServiceEntry *entry = NULL;
			uint16_t                        size;
			char                           *string;
			if (!oneTime)
			{
				oneTime = 1;
				otSrpClientSetCallback(openthread_get_default_instance(), on_srp_client_updated, NULL);
				if (otSrpClientSetHostName(openthread_get_default_instance(), realhostname) != OT_ERROR_NONE)
					LOG_INF("Cannot set SRP host name");
				if (otSrpClientEnableAutoHostAddress(openthread_get_default_instance()) != OT_ERROR_NONE)
					LOG_INF("Cannot set SRP host address to auto");
				entry = otSrpClientBuffersAllocateService(openthread_get_default_instance());
				string = otSrpClientBuffersGetServiceEntryInstanceNameString(entry, &size); // make sure "service_instance" is not bigger than "size"!
				memcpy(string, realinstance, sizeof(realinstance)+1);
				string = otSrpClientBuffersGetServiceEntryServiceNameString(entry, &size);
				memcpy(string, realinstance, sizeof(realinstance)+1); // make sure "service_name" is not bigger than "size"!;
				entry->mService.mNumTxtEntries = 0;
				entry->mService.mPort = 49154;
				if (otSrpClientAddService(openthread_get_default_instance(), &entry->mService) != OT_ERROR_NONE)
					LOG_INF("Cannot add service to SRP client");
				else
					LOG_INF("SRP client service added succesfully");
				otSrpClientEnableAutoStartMode(openthread_get_default_instance(), NULL, NULL);
				entry = NULL;
			}
			break;

		case OT_DEVICE_ROLE_DISABLED:
		case OT_DEVICE_ROLE_DETACHED:
		default:
			dk_set_led_off(OT_CONNECTION_LED);
			break;
		}
	}
}
static struct openthread_state_changed_cb ot_state_chaged_cb = { .state_changed_cb =
									 on_thread_state_changed };

int main(void)
{
	int ret;

	LOG_INF("main.c entry point.");

	// enable USB
	ret = usb_enable(NULL);
	if (ret != 0) {
		LOG_ERR("Failed to enable USB");
		return 0;
	}

	
	memcpy(realhostname, hostname, sizeof(hostname));
	memcpy(realinstance, service_instance, sizeof(service_instance));
	uint32_t rn = sys_rand32_get();
	LOG_INF("random uint32_t is: %u\n", rn);
	snprintf(realhostname+sizeof(hostname)-1, SRP_CLIENT_RAND_SIZE, "%u", rn);
	snprintf(realinstance+sizeof(service_instance)-1, SRP_CLIENT_RAND_SIZE, "%u", rn);
	LOG_INF("hostname is: %s\n", realhostname);
	LOG_INF("service instance is: %s\n", realhostname);
	LOG_INF("Start CoAP-server sample");

	ret = ot_coap_init(&on_light_request);
	if (ret) {
		LOG_ERR("Could not initialize OpenThread CoAP");
		goto end;
	}

	ret = dk_leds_init();
	if (ret) {
		LOG_ERR("Could not initialize leds, err code: %d", ret);
		goto end;
	}

	ret = dk_buttons_init(on_button_changed);
	if (ret) {
		LOG_ERR("Cannot init buttons (error: %d)", ret);
		goto end;
	}

	openthread_state_changed_cb_register(openthread_get_default_context(), &ot_state_chaged_cb);
	openthread_start(openthread_get_default_context());

end:
	return 0;
}
