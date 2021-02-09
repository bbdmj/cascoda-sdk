#include <unistd.h>

#include <openthread/cli.h>
#include <openthread/diag.h>
#include <openthread/tasklet.h>
#include <openthread/thread.h>
#include <platform.h>
#include "cascoda-util/cascoda_tasklet.h"

#include "ca-ot-util/cascoda_dns.h"
#include "cascoda-bm/cascoda_evbme.h"
#include "cascoda-bm/cascoda_interface.h"
#include "cascoda-bm/cascoda_serial.h"
#include "cascoda-bm/cascoda_types.h"
#include "cascoda-util/cascoda_time.h"
#include "ca821x_api.h"

#include "port/oc_assert.h"
#include "port/oc_clock.h"
#include "oc_api.h"
#include "oc_buffer_settings.h"
#include "sntp_helper.h"

#include "ocf_application.h"

static otCliCommand ocfCommand;

/**
 * Handle application specific commands.
 */
static int ot_serial_dispatch(uint8_t *buf, size_t len, struct ca821x_dev *pDeviceRef)
{
	int ret = 0;

	if (buf[0] == OT_SERIAL_DOWNLINK)
	{
		PlatformUartReceive(buf + 2, buf[1]);
		ret = 1;
	}

	// switch clock otherwise chip is locking up as it loses external clock
	if (((buf[0] == EVBME_SET_REQUEST) && (buf[2] == EVBME_RESETRF)) || (buf[0] == EVBME_HOST_CONNECTED))
	{
		EVBME_SwitchClock(pDeviceRef, 0);
	}
	return ret;
}

static void ot_state_changed(uint32_t flags, void *context)
{
	(void)context;

	if (flags & OT_CHANGED_THREAD_ROLE)
	{
		otDeviceRole role = otThreadGetDeviceRole(OT_INSTANCE);
		PRINT("Role: %d\n", role);

		bool must_update_rtc = (SNTP_GetState() == NO_TIME);
		if ((role != OT_DEVICE_ROLE_DISABLED && role != OT_DEVICE_ROLE_DETACHED) && must_update_rtc)
			SNTP_Update();
	}
}

static void signal_event_loop(void)
{
}

/**
* main application.
* intializes the global variables
* registers and starts the handler
* handles (in a loop) the next event.
* shuts down the stack
*/
int main(void)
{
	int               init;
	oc_clock_time_t   next_event;
	u8_t              StartupStatus;
	struct ca821x_dev dev;
	cascoda_serial_dispatch = ot_serial_dispatch;

	ca821x_api_init(&dev);

	// Initialisation of Chip and EVBME
	StartupStatus = EVBMEInitialise(CA_TARGET_NAME, &dev);
	BSP_RTCInitialise();

	PlatformRadioInitWithDev(&dev);

	// OpenThread Configuration
	OT_INSTANCE = otInstanceInitSingle();

	otIp6SetEnabled(OT_INSTANCE, true);

	// Enable the OpenThread CLI and add a custom command.
	otCliUartInit(OT_INSTANCE);
	ocfCommand.mCommand = handle_ocf_light_server;
	ocfCommand.mName    = "ocflight";
	otCliSetUserCommands(&ocfCommand, 1);

	if (otDatasetIsCommissioned(OT_INSTANCE))
		otThreadSetEnabled(OT_INSTANCE, true);

	oc_assert(OT_INSTANCE);

	DNS_Init(OT_INSTANCE);
	SNTP_Init();

#ifdef OC_RETARGET
	oc_assert(otPlatUartEnable() == OT_ERROR_NONE);
#endif

	otSetStateChangedCallback(OT_INSTANCE, ot_state_changed, NULL);

	PRINT("Used input file : \"../iotivity-lite-lightdevice/out_codegeneration_merged.swagger.json\"\n");
	PRINT("OCF Server name : \"server_lite_53868\"\n");

	initialize_variables();

	/* initializes the handlers structure */
	static const oc_handler_t handler = {.init               = app_init,
	                                     .signal_event_loop  = signal_event_loop,
	                                     .register_resources = register_resources
#ifdef OC_CLIENT
	                                     ,
	                                     .requests_entry = 0
#endif
	};

#ifdef OC_SECURITY
	PRINT("Intialize Secure Resources\n");
	oc_storage_config("./devicebuilderserver_creds");
#endif /* OC_SECURITY */

#ifdef OC_SECURITY
	/* please comment out if the server:
    - has no display capabilities to display the PIN value
    - server does not require to implement RANDOM PIN (oic.sec.doxm.rdp) onboarding mechanism
  */
	//oc_set_random_pin_callback(random_pin_cb, NULL);
#endif /* OC_SECURITY */

	oc_set_factory_presets_cb(factory_presets_cb, NULL);

	/* start the stack */
	init = oc_main_init(&handler);

	oc_set_max_app_data_size(CASCODA_MAX_APP_DATA_SIZE);
	oc_set_mtu_size(1232);

	if (init < 0)
	{
		PRINT("oc_main_init failed %d.\n", init);
	}

	PRINT("OCF server \"server_lite_53868\" running, waiting on incoming connections.\n");

	while (1)
	{
		cascoda_io_handler(&dev);
		otTaskletsProcess(OT_INSTANCE);
		oc_main_poll();
	}

	/* shut down the stack, should not get here */
	oc_main_shutdown();
	return 0;
}
