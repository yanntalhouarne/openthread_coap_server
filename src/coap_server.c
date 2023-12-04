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

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/devicetree.h>

#include "ot_coap_utils.h"
#include "ot_srp_config.h"

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/* Data of ADC io-channels specified in devicetree. */
static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
			     DT_SPEC_AND_COMMA)
};

LOG_MODULE_REGISTER(coap_server, CONFIG_COAP_SERVER_LOG_LEVEL);

#define OT_CONNECTION_LED 3
#define PROVISIONING_LED 2
#define LIGHT_LED 0
#define WATER_PUMP 4

#define PUMP_MAX_ACTIVE_TIME 10 // seconds
#define ADC_TIMER_PERIOD 1 // seconds

// FW version
const char fw_version[] = SRP_CLIENT_INFO;

// ADC globals
uint16_t buf;
struct adc_sequence sequence = {
	.buffer = &buf,
	/* buffer size in bytes, not number of samples */
	.buffer_size = sizeof(buf),
};

struct fw_version {
	// FW version
	const char * fw_version_buf;
	uint8_t fw_version_size;
};

struct fw_version fw = {
	.fw_version_buf = fw_version,
	.fw_version_size = sizeof(fw_version),
};

int16_t temperature = 0;

/* timer */
static struct k_timer pump_timer;
static struct k_timer adc_timer;

/* fuel gauge*/
const struct device *const dev_fuelgauge = DEVICE_DT_GET_ANY(maxim_max17048);
struct fuel_gauge_get_property props_fuel_gauge[] = {
	{
		.property_type = FUEL_GAUGE_RUNTIME_TO_EMPTY,
	},
	{
		.property_type = FUEL_GAUGE_RUNTIME_TO_FULL,
	},
	{
		.property_type = FUEL_GAUGE_STATE_OF_CHARGE,
	},
	{
		.property_type = FUEL_GAUGE_VOLTAGE,
	}
};

/* hostname */
const char hostname[] = SRP_CLIENT_HOSTNAME;
const char service_instance[] = SRP_CLIENT_SERVICE_INSTANCE;
#ifdef SRP_CLIENT_RNG
char realhostname[sizeof(hostname)+SRP_CLIENT_RAND_SIZE+1] = {0};
char realinstance[sizeof(service_instance)+SRP_CLIENT_RAND_SIZE+1] = {0};
#elif SRP_CLIENT_UNIQUE
char realhostname[sizeof(hostname)+SRP_CLIENT_UNIQUE_SIZE+1] = {0};
char realinstance[sizeof(service_instance)+SRP_CLIENT_UNIQUE_SIZE+1] = {0};
#endif

const char service_name[] = SRP_SERVICE_NAME;

struct fw_version on_info_request()
{
	return fw;
}

static void on_light_request(uint8_t command)
{
	switch (command) {
	case THREAD_COAP_UTILS_LIGHT_CMD_ON:
		if (coap_is_pump_active() == false)
		{
			coap_activate_pump();
			dk_set_led_on(LIGHT_LED);
			dk_set_led_on(WATER_PUMP);
			k_timer_start(&pump_timer, K_SECONDS(PUMP_MAX_ACTIVE_TIME), K_NO_WAIT); // pump will be active for 5 seconds, unless a stop command is received
		}
		break;

	case THREAD_COAP_UTILS_LIGHT_CMD_OFF:
		if (coap_is_pump_active() == true)
		{
			coap_diactivate_pump();
			dk_set_led_off(LIGHT_LED);
			dk_set_led_off(WATER_PUMP);
			k_timer_stop(&pump_timer);
		}
		break;

	default:
		break;
	}
}


static int8_t on_temperature_request()
{
	int err;
	int32_t val_mv;

	// /* READ TEMPERATURE */
	// for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {

	// 	(void)adc_sequence_init_dt(&adc_channels[i], &sequence);

	// 	err = adc_read(adc_channels[i].dev, &sequence);
	// 	if (err < 0) {
	// 		LOG_ERR("Could not read (%d)\n", err);
	// 		continue;
	// 	}

	// 	/* conversion to mV may not be supported, skip if not */
	// 	val_mv = buf;
	// 	err = adc_raw_to_millivolts_dt(&adc_channels[i],
	// 						&val_mv);
	// 	if (err < 0) {
	// 		LOG_ERR(" (value in mV not available)\n");
	// 	}
	// }
	// temperature = (uint8_t)val_mv;

	/* READ BATTERY SOC */
	err = fuel_gauge_get_prop(dev_fuelgauge, props_fuel_gauge, ARRAY_SIZE(props_fuel_gauge));
	if (err < 0) {
		LOG_INF("Error: cannot get properties\n");
	} else {
		if (err != 0) {
			LOG_INF("Warning: (Fuel-gauge) Some properties failed\n");
		}
		if (props_fuel_gauge[2].status == 0) {
			LOG_INF("Charge %d%%\n", props[2].value.state_of_charge);
		} else {
			LOG_INF(
			"Property FUEL_GAUGE_STATE_OF_CHARGE failed with error %d\n",
			props_fuel_gauge[2].status
			);
		}
	}

	temperature = (uint8_t)props_fuel_gauge[2].value.state_of_charge;

	LOG_INF("Temperature is %d\n", temperature);

	return temperature;
}

static void on_button_changed(uint32_t button_state, uint32_t has_changed)
{
	uint32_t buttons = button_state & has_changed;

	if (buttons & 4) {
		//k_work_submit(&provisioning_work);
	}
}

void srp_client_generate_name()
{
	#ifdef SRP_CLIENT_UNIQUE
		LOG_INF("Appending device ID to hostname");
		// first copy the hostname and service instance defined defined by SRP_CLIENT_HOSTNAME and SRP_CLIENT_SERVICE_INSTANCE, respectively
		memcpy(realhostname, hostname, sizeof(hostname));
		memcpy(realinstance, service_instance, sizeof(service_instance));
		// get a device ID
		uint32_t device_id = NRF_FICR->DEVICEID[0];
		// append the random number as a string to the hostname and service_instance buffers (numbe of digits is defined by SRP_CLIENT_RAND_SIZE)
		snprintf(realhostname+sizeof(hostname)-1, SRP_CLIENT_UNIQUE_SIZE+2, "-%x", device_id);
		snprintf(realinstance+sizeof(service_instance)-1, SRP_CLIENT_UNIQUE_SIZE+2, "-%x", device_id);
		LOG_INF("hostname is: %s\n", realhostname);
		LOG_INF("service instance is: %s\n", realinstance);
	#elif SRP_CLIENT_RNG
		LOG_INF("Appending random number to hostname");
		/* append a random number of size SRP_CLIENT_RAND_SIZE to the service hostname and service instance string buffers */
		// first copy the hostname and service instance defined defined by SRP_CLIENT_HOSTNAME and SRP_CLIENT_SERVICE_INSTANCE, respectively
		memcpy(realhostname, hostname, sizeof(hostname));
		memcpy(realinstance, service_instance, sizeof(service_instance));
		// get a random uint32_t (true random, hw based)
		uint32_t rn = sys_rand32_get();
		// append the random number as a string to the hostname and service_instance buffers (numbe of digits is defined by SRP_CLIENT_RAND_SIZE)
		snprintf(realhostname+sizeof(hostname)-1, SRP_CLIENT_RAND_SIZE+2, "-%x", rn);
		snprintf(realinstance+sizeof(service_instance)-1, SRP_CLIENT_RAND_SIZE+2, "-%x", rn);
		LOG_INF("hostname is: %s\n", realhostname);
		LOG_INF("service instance is: %s\n", realinstance);
	#else
		LOG_INF("hostname is: %s\n", hostname);
		LOG_INF("service instance is: %s\n", service_instance);
	#endif
}

void on_srp_client_updated(otError aError, const otSrpClientHostInfo *aHostInfo, const otSrpClientService *aServices, const otSrpClientService *aRemovedServices, void *aContext);

void on_srp_client_updated(otError aError, const otSrpClientHostInfo *aHostInfo, const otSrpClientService *aServices, const otSrpClientService *aRemovedServices, void *aContext)
{
	LOG_INF("SRP callback: %s", otThreadErrorToString(aError));
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

				// set the SRP update callback
				otSrpClientSetCallback(openthread_get_default_instance(), on_srp_client_updated, NULL);
				// set the service hostname
				#if defined SRP_CLIENT_RNG || defined SRP_CLIENT_UNIQUE
				if (otSrpClientSetHostName(openthread_get_default_instance(), realhostname) != OT_ERROR_NONE)
				#else
				if (otSrpClientSetHostName(openthread_get_default_instance(), hostname) != OT_ERROR_NONE)
				#endif
					LOG_INF("Cannot set SRP host name");
				// set address to auto
				if (otSrpClientEnableAutoHostAddress(openthread_get_default_instance()) != OT_ERROR_NONE)
					LOG_INF("Cannot set SRP host address to auto");
				// allocate service buffers from OT SRP API
				entry = otSrpClientBuffersAllocateService(openthread_get_default_instance());
				// get the service instance name string buffer from OT SRP API
				string = otSrpClientBuffersGetServiceEntryInstanceNameString(entry, &size); // make sure "service_instance" is not bigger than "size"!
				// copy the service instance
				#if defined SRP_CLIENT_RNG || defined SRP_CLIENT_UNIQUE
				memcpy(string, realinstance, sizeof(realinstance)+1);
				#else
				memcpy(string, service_instance, sizeof(service_instance)+1);
				#endif
				// get the service name string buffer from OT SRP API
				string = otSrpClientBuffersGetServiceEntryServiceNameString(entry, &size);
				// copy the service name (_ot._udp)
				memcpy(string, service_name, sizeof(service_name)+1); // make sure "service_name" is not bigger than "size"!;
				// configure service
				entry->mService.mNumTxtEntries = 0;
				entry->mService.mPort = 49154;
				// add service
				if (otSrpClientAddService(openthread_get_default_instance(), &entry->mService) != OT_ERROR_NONE)
					LOG_INF("Cannot add service to SRP client");
				else
					LOG_INF("Adding SRP client service...");
				// start SRP client (and set to auto-mode)
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

static void on_pump_timer_expiry(struct k_timer *timer_id)
{
	ARG_UNUSED(timer_id);

	coap_diactivate_pump();

	dk_set_led_off(LIGHT_LED);
	dk_set_led_off(WATER_PUMP);

	k_timer_stop(&pump_timer);

}

static void on_adc_timer_expiry(struct k_timer *timer_id)
{
	ARG_UNUSED(timer_id);
	int err;
	int32_t val_mv;

	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {


		(void)adc_sequence_init_dt(&adc_channels[i], &sequence);

		err = adc_read(adc_channels[i].dev, &sequence);
		if (err < 0) {
			LOG_ERR("Could not read (%d)\n", err);
			continue;
		}

		/* conversion to mV may not be supported, skip if not */
		val_mv = buf;
		err = adc_raw_to_millivolts_dt(&adc_channels[i],
							&val_mv);
		if (err < 0) {
			LOG_ERR(" (value in mV not available)\n");
		}
	}

	temperature = (int16_t)val_mv;
}

int main(void)
{
	int ret;

	/* enable USB */
	ret = usb_enable(NULL);
	if (ret != 0) {
		LOG_ERR("Failed to enable USB");
		goto end;
	}

	k_sleep(K_MSEC(5000));

	/* Fuel Gauge */

	if (dev_fuelgauge == NULL) {
		LOG_ERR("\nError: no device found.\n");
		goto end;
	}
	if (!device_is_ready(dev_fuelgauge)) {
		LOG_ERR("\nError: Device \"%s\" is not ready; "
		       "check the driver initialization logs for errors.\n",
		       dev_fuelgauge->name);
		goto end;
	}
	LOG_INF("Found device \"%s\", getting fuel gauge data\n", dev_fuelgauge->name);
	if (dev_fuelgauge == NULL) {
		goto end;
	}

	ret = fuel_gauge_get_prop(dev_fuelgauge, props_fuel_gauge, ARRAY_SIZE(props_fuel_gauge));
	if (ret < 0) {
		LOG_INF("Error: cannot get properties\n");
	} else {
		if (ret != 0) {
			LOG_INF("Warning: Some properties failed\n");
		}

		if (props_fuel_gauge[0].status == 0) {
			LOG_INF("Time to empty %d\n", props_fuel_gauge[0].value.runtime_to_empty);
		} else {
			LOG_INF(
			"Property FUEL_GAUGE_RUNTIME_TO_EMPTY failed with error %d\n",
			props_fuel_gauge[0].status
			);
		}

		if (props_fuel_gauge[1].status == 0) {
			LOG_INF("Time to full %d\n", props_fuel_gauge[1].value.runtime_to_full);
		} else {
			LOG_INF(
			"Property FUEL_GAUGE_RUNTIME_TO_FULL failed with error %d\n",
			props_fuel_gauge[1].status
			);
		}

		if (props_fuel_gauge[2].status == 0) {
			LOG_INF("Charge %d%%\n", props_fuel_gauge[2].value.state_of_charge);
		} else {
			LOG_INF(
			"Property FUEL_GAUGE_STATE_OF_CHARGE failed with error %d\n",
			props_fuel_gauge[2].status
			);
		}

		if (props_fuel_gauge[3].status == 0) {
			LOG_INF("Voltage %d\n", props_fuel_gauge[3].value.voltage);
		} else {
			LOG_INF(
			"Property FUEL_GAUGE_VOLTAGE failed with error %d\n",
			props_fuel_gauge[3].status
			);
		}
	}

	/* Configure channels individually prior to sampling. */
	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
		if (!device_is_ready(adc_channels[i].dev)) {
			LOG_ERR("ADC controller device not ready\n");
			goto end;
		}
		ret = adc_channel_setup_dt(&adc_channels[i]);
		if (ret < 0) {
			LOG_ERR("Could not setup channel #%d (%d)\n", i, ret);
			goto end;
		}
	}

	/* generate a SRP client name to be advertised (mode defined in ot_srp_config.h macros) */
	srp_client_generate_name();

	LOG_INF("Start CoAP-server sample");
	ret = ot_coap_init(&on_light_request, &on_temperature_request, &on_info_request);
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

	dk_set_led_on(OT_CONNECTION_LED);
	k_sleep(K_MSEC(100));
	dk_set_led_off(OT_CONNECTION_LED);
	k_sleep(K_MSEC(100));
	dk_set_led_on(OT_CONNECTION_LED);
	k_sleep(K_MSEC(100));
	dk_set_led_off(OT_CONNECTION_LED);
	k_sleep(K_MSEC(100));
	dk_set_led_on(OT_CONNECTION_LED);
	k_sleep(K_MSEC(100));
	dk_set_led_off(OT_CONNECTION_LED);
	k_sleep(K_MSEC(100));

	dk_set_led_on(LIGHT_LED);
	k_sleep(K_MSEC(100));
	dk_set_led_off(LIGHT_LED);
	k_sleep(K_MSEC(100));
	dk_set_led_on(LIGHT_LED);
	k_sleep(K_MSEC(100));
	dk_set_led_off(LIGHT_LED);
	k_sleep(K_MSEC(100));
	dk_set_led_on(LIGHT_LED);
	k_sleep(K_MSEC(100));
	dk_set_led_off(LIGHT_LED);
	k_sleep(K_MSEC(100));

	// dk_set_led_on(WATER_PUMP);
	// k_sleep(K_MSEC(10000));
	// dk_set_led_off(WATER_PUMP);

	/* Timer */
	k_timer_init(&pump_timer, on_pump_timer_expiry, NULL);
	k_timer_init(&adc_timer, on_adc_timer_expiry, NULL);
	/*
		If we want to get the temperature value periodically, start the timer.
		Otherwise, the ADC will be check only upon a tempereature GET request
		from coap server.
	*/
	//k_timer_start(&adc_timer, K_SECONDS(ADC_TIMER_PERIOD), K_SECONDS(ADC_TIMER_PERIOD));

	openthread_state_changed_cb_register(openthread_get_default_context(), &ot_state_chaged_cb);
	openthread_start(openthread_get_default_context());

end:
	return 0;
}
