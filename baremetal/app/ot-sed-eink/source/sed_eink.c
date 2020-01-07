/******************************************************************************/
/******************************************************************************/
/****** Cascoda Ltd. 2019                                                ******/
/******************************************************************************/
/******************************************************************************/
/****** Openthread standalone SED w/ basic CoAP server discovery & reporting **/
/******************************************************************************/
/******************************************************************************/
#include <stdlib.h>
#include <string.h>

#include "cascoda-bm/cascoda_evbme.h"
#include "cascoda-bm/cascoda_interface.h"
#include "cascoda-bm/cascoda_sensorif.h"
#include "cascoda-bm/cascoda_serial.h"
#include "cascoda-bm/cascoda_time.h"
#include "cascoda-bm/cascoda_types.h"
#include "ca821x_api.h"
#include "ca821x_endian.h"

#include "openthread/coap.h"
#include "openthread/instance.h"
#include "openthread/link.h"
#include "openthread/tasklet.h"
#include "openthread/thread.h"
#include "platform.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "sif_eink.h"
#include "uzlib.h"

#define SuccessOrExit(aCondition) \
	do                            \
	{                             \
		if ((aCondition) != 0)    \
		{                         \
			goto exit;            \
		}                         \
	} while (0)

/******************************************************************************/
/****** Power Consumption Configuration                                  ******/
/******************************************************************************/
// How long to wait between discover request retries
const int DISCOVER_FAIL_RETRY_MS = 300;
// How long to put the device to sleep if it cannot establish a connection at all
// Not currently implemented
const int DISCOVER_TIMEOUT_SLEEP_MS = 30 * 1000;

// How long to sleep for after having received an image
const int IMAGE_OK_SLEEP_MS = 8 * 1000;
// Devices sleep for IMAGE_OK_SLEEP_MS + [0, IMAGE_RANDOM_SLEEP_MS)
const int IMAGE_RANDOM_SLEEP_MS = 4 * 1000;
// How long to wait between resending image GET requests
const int IMAGE_FAIL_RETRY_MS = 5 * 1000;

/******************************************************************************/
/****** Application name                                                 ******/
/******************************************************************************/
const char *APP_NAME = "OT SED";

const char *uriCascodaDiscover    = "ca/di";
const char *uriCascodaTemperature = "ca/te";
const char *uriCascodaImage       = "ca/img";
const char *uriCascodaQueryOption = "id=004.gz";

/******************************************************************************/
/****** Single instance                                                  ******/
/******************************************************************************/
otInstance *       OT_INSTANCE;
struct ca821x_dev  dev;
struct ca821x_dev *CA_DEVICE;

static bool         isConnected  = false;
static int          timeoutCount = 0;
static otIp6Address serverIp;
static uint32_t     appNextSendTime = 5000;

/******************************************************************************/
/****** FreeRTOS-related globals                                         ******/
/******************************************************************************/
TaskHandle_t      CommsTaskHandle;
SemaphoreHandle_t CommsMutexHandle;

// The size matches the maximum size of a CoAP packet
static unsigned char message_buffer[1024];
// length of the message currently in the buffer
static uint16_t message_length = 0;
// The resolution of the image is 296x128
#define IMAGE_SIZE (296 * 128 / 8)
static unsigned char image_buffer[IMAGE_SIZE];

void initialise_communications();

/******************************************************************************/
/***************************************************************************/ /**
 * \brief Checks current device status and goes to sleep if nothing is happening
 *******************************************************************************
 ******************************************************************************/
// For now, the application will not use a dedicated power down mode when sleeping.
// Therefore, this function is redundant.
static void SleepTask(void)
{
	//Application check
	uint32_t appTimeLeft = appNextSendTime - BSP_ReadAbsoluteTime();

	// If there is no work to do...
	if (!otTaskletsArePending(OT_INSTANCE))
	{
		otLinkModeConfig linkMode = otThreadGetLinkMode(OT_INSTANCE);

		// Sleep until the next alarm if...
		if (linkMode.mDeviceType == 0                                     // device is Minimal Thread Device
		    && linkMode.mRxOnWhenIdle == 0                                // receiver is off when idling
		    && otThreadGetDeviceRole(OT_INSTANCE) == OT_DEVICE_ROLE_CHILD // device is not a router
		    && !otLinkIsInTransmitState(OT_INSTANCE)                      // MAC is not currently transmitting
		    && !PlatformIsExpectingIndication())                          // MAC is not about to send an indication
		{
			uint32_t idleTimeLeft = PlatformGetAlarmMilliTimeout();
			if (idleTimeLeft > appTimeLeft)
				idleTimeLeft = appTimeLeft;

			if (idleTimeLeft > 5)
			{
				struct ModuleSpecialPins special_pins = BSP_GetModuleSpecialPins();
				BSP_ModuleSetGPIOPin(special_pins.LED_RED, LED_OFF);
				BSP_ModuleSetGPIOPin(special_pins.LED_GREEN, LED_OFF);
				PlatformSleep(idleTimeLeft);
				BSP_ModuleSetGPIOPin(special_pins.LED_GREEN, LED_ON);
			}
		}
	}
}

/******************************************************************************/
/***************************************************************************/ /**
 * \brief Initialisation function
 *******************************************************************************
 ******************************************************************************/
static void NANO120_Initialise(u8_t status, struct ca821x_dev *pDeviceRef)
{
	struct ModuleSpecialPins special_pins = BSP_GetModuleSpecialPins();
	/* register LED_G */
	BSP_ModuleRegisterGPIOOutput(special_pins.LED_GREEN, MODULE_PIN_TYPE_LED);
	/* register LED_R */
	BSP_ModuleRegisterGPIOOutput(special_pins.LED_RED, MODULE_PIN_TYPE_LED);

	if (status == CA_ERROR_FAIL)
	{
		BSP_ModuleSetGPIOPin(special_pins.LED_RED, LED_ON);
		return;
	}

	BSP_ModuleSetGPIOPin(special_pins.LED_RED, LED_OFF);
	BSP_ModuleSetGPIOPin(special_pins.LED_GREEN, LED_ON);

	EVBME_SwitchClock(pDeviceRef, 1);

	//NANO120_APP_SaveOrRestoreAddress();
} // End of NANO120_Initialise()

// Callback called by OpenThread when there is work to
void otTaskletsSignalPending(otInstance *aInstance)
{
	(void)aInstance;
}

/******************************************************************************/
/***************************************************************************/ /**
 * \brief Handle the response to the server discover, and register the server
 * locally.
 *******************************************************************************
 ******************************************************************************/
static void handleServerDiscoverResponse(void *               aContext,
                                         otMessage *          aMessage,
                                         const otMessageInfo *aMessageInfo,
                                         otError              aError)
{
	if (aError != OT_ERROR_NONE)
		return;
	if (isConnected)
		return;

	uint16_t length = otMessageGetLength(aMessage) - otMessageGetOffset(aMessage);

	if (length < sizeof(otIp6Address))
		return;

	otMessageRead(aMessage, otMessageGetOffset(aMessage), &serverIp, sizeof(otIp6Address));
	isConnected  = true;
	timeoutCount = 0;
}

/******************************************************************************/
/***************************************************************************/ /**
 * \brief Send a multicast cascoda 'server discover' coap message. This is a
 * non-confirmable get request, and the get responses will be handled by the
 * 'handleServerDiscoverResponse' function.
 *******************************************************************************
 ******************************************************************************/
static otError sendServerDiscover(void)
{
	otError       error   = OT_ERROR_NONE;
	otMessage *   message = NULL;
	otMessageInfo messageInfo;
	otIp6Address  coapDestinationIp;

	//allocate message buffer
	message = otCoapNewMessage(OT_INSTANCE, NULL);
	if (message == NULL)
	{
		error = OT_ERROR_NO_BUFS;
		goto exit;
	}

	//Build CoAP header
	//Realm local all-nodes multicast - this of course generates some traffic, so shouldn't be overused
	SuccessOrExit(error = otIp6AddressFromString("FF03::1", &coapDestinationIp));
	otCoapMessageInit(message, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_GET);
	otCoapMessageGenerateToken(message, 2);
	SuccessOrExit(error = otCoapMessageAppendUriPathOptions(message, uriCascodaDiscover));

	memset(&messageInfo, 0, sizeof(messageInfo));
	messageInfo.mPeerAddr = coapDestinationIp;
	messageInfo.mPeerPort = OT_DEFAULT_COAP_PORT;

	//send
	error = otCoapSendRequest(OT_INSTANCE, message, &messageInfo, &handleServerDiscoverResponse, NULL);

exit:
	if (error && message)
	{
		//error, we have to free
		otMessageFree(message);
	}
	return error;
}

void decompress_data()
{
	// Decompress the message using uzlib

	// Decompressed length, can be mismatched by one byte
	unsigned int dlen = IMAGE_SIZE + 1;
	// How many bytes to decompress in one pass
	const int OUT_CHUNK_SIZE = 1;
	int       res;

	/* -- decompress data -- */

	struct uzlib_uncomp d;
	uzlib_uncompress_init(&d, NULL, 0);

	/* all 3 fields below must be initialized by user */
	d.source         = message_buffer;
	d.source_limit   = message_buffer + IMAGE_SIZE - 4;
	d.source_read_cb = NULL;

	res = uzlib_gzip_parse_header(&d);
	if (res != TINF_OK)
	{
		// Could not parse GZIP header
		configASSERT(0);
	}

	d.dest_start = d.dest = image_buffer;

	while (dlen)
	{
		unsigned int chunk_len = dlen < OUT_CHUNK_SIZE ? dlen : OUT_CHUNK_SIZE;
		d.dest_limit           = d.dest + chunk_len;
		res                    = uzlib_uncompress_chksum(&d);
		dlen -= chunk_len;
		if (res != TINF_OK)
		{
			break;
		}
	}

	if (res != TINF_DONE)
	{
		// Error during decompression
		configASSERT(0);
	}
}

static void handleImageResponse(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo, otError aError)
{
	if (aError == OT_ERROR_RESPONSE_TIMEOUT && timeoutCount++ > 3)
	{
		isConnected = false;
	}
	else if (aError == OT_ERROR_NONE)
	{
		timeoutCount = 0;
	}

	if (aError != OT_ERROR_NONE)
		return;

	isConnected = true;

	// Put the data in the message buffer
	message_length = otMessageGetLength(aMessage) - otMessageGetOffset(aMessage);
	otMessageRead(aMessage, otMessageGetOffset(aMessage), &message_buffer, message_length);

	// Turn off the radio if you have successfully received an image
	otInstanceFinalize(OT_INSTANCE);

	// Decompress the data and put it in the image buffer
	decompress_data();

	// Write the received data to the display
	EINK_Initialise(&lut_full_update);
	EINK_Display(image_buffer);
	EINK_DeepSleep();

	// Get a random number, to randomise the sleep time
	uint8_t ranLen = 0;
	uint8_t random[2];
	HWME_GET_request_sync(HWME_RANDOMNUM, &ranLen, random, &dev);

	// Sleep until you must get a new image
	int random_delay_ms = (GETLE16(random)) % IMAGE_RANDOM_SLEEP_MS;
	EVBME_PowerDown(PDM_DPD, IMAGE_OK_SLEEP_MS + random_delay_ms, &dev);

	// Should not get here
	for (;;)
		;
}

static otError sendImageRequest(void)
{
	otError       error   = OT_ERROR_NONE;
	otMessage *   message = NULL;
	otMessageInfo messageInfo;

	//allocate message buffer
	message = otCoapNewMessage(OT_INSTANCE, NULL);
	if (message == NULL)
	{
		error = OT_ERROR_NO_BUFS;
		goto exit;
	}

	//Build CoAP header
	otCoapMessageInit(message, OT_COAP_TYPE_CONFIRMABLE, OT_COAP_CODE_GET);
	otCoapMessageGenerateToken(message, 2);
	SuccessOrExit(error = otCoapMessageAppendUriPathOptions(message, uriCascodaImage));

	//Append URI option that identifies which image to display
	SuccessOrExit(error = otCoapMessageAppendUriQueryOption(message, uriCascodaQueryOption));

	memset(&messageInfo, 0, sizeof(messageInfo));
	messageInfo.mPeerAddr = serverIp;
	messageInfo.mPeerPort = OT_DEFAULT_COAP_PORT;

	//send
	error = otCoapSendRequest(OT_INSTANCE, message, &messageInfo, &handleImageResponse, NULL);

exit:
	if (error && message)
	{
		//error, we have to free
		otMessageFree(message);
	}
	return error;
}

void ImageHandlerTask(void *unused)
{
	(void)unused;

	/* This task calls secure-side functions (namely, functions in the BSP)
	 * therefore it must allocate a secure context before doing so. */
	portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);

	for (;;)
	{
		if (isConnected)
		{
			xSemaphoreTake(CommsMutexHandle, portMAX_DELAY);
			sendImageRequest();
			xSemaphoreGive(CommsMutexHandle);

			// Block after requesting data
			const TickType_t delay = IMAGE_FAIL_RETRY_MS / portTICK_PERIOD_MS;
			vTaskDelay(delay);
		}
		else
		{
			xSemaphoreTake(CommsMutexHandle, portMAX_DELAY);
			sendServerDiscover();
			xSemaphoreGive(CommsMutexHandle);

			// Block after attempting to connect
			const TickType_t delay = DISCOVER_FAIL_RETRY_MS / portTICK_PERIOD_MS;
			vTaskDelay(delay);
		}
	}
}

static void CommsTask(void *unused)
{
	(void)unused;

	/* This task calls secure-side functions (namely, functions in the BSP)
	 * therefore it must allocate a secure context before doing so. */
	portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);

	for (;;)
	{
		xSemaphoreTake(CommsMutexHandle, portMAX_DELAY);

		PlatformAlarmProcess(OT_INSTANCE);
		cascoda_io_handler(CA_DEVICE);
		otTaskletsProcess(OT_INSTANCE);

		xSemaphoreGive(CommsMutexHandle);
	}
} // End of CommsTask()

void initialise_communications()
{
	u8_t StartupStatus;
	ca821x_api_init(&dev);
	CA_DEVICE = &dev;

	// Initialisation of Chip and EVBME
	StartupStatus = EVBMEInitialise(APP_NAME, &dev);

	// Insert Application-Specific Initialisation Routines here
	NANO120_Initialise(StartupStatus, &dev);

	PlatformRadioInitWithDev(&dev);

	OT_INSTANCE = otInstanceInitSingle();

	/* Setup Thread stack with hard coded demo parameters */
	otLinkModeConfig linkMode    = {0};
	linkMode.mRxOnWhenIdle       = true;
	linkMode.mSecureDataRequests = true;

	otMasterKey key = {0xa8, 0xcd, 0xb0, 0x47, 0x74, 0xf3, 0xec, 0x1f, 0xc8, 0xbf, 0x8f, 0xce, 0xbe, 0x51, 0x91, 0x7f};
	otLinkSetPollPeriod(OT_INSTANCE, 5000);
	otIp6SetEnabled(OT_INSTANCE, true);
	otLinkSetPanId(OT_INSTANCE, 0x359b);
	// Child times out after 5 seconds
	otThreadSetChildTimeout(OT_INSTANCE, 5);
	otThreadSetLinkMode(OT_INSTANCE, linkMode);
	otThreadSetMasterKey(OT_INSTANCE, &key);
	otLinkSetChannel(OT_INSTANCE, 23);
	otThreadSetEnabled(OT_INSTANCE, true);

	otCoapStart(OT_INSTANCE, OT_DEFAULT_COAP_PORT);

	otTaskletsProcess(OT_INSTANCE);
}

/******************************************************************************/
/******************************************************************************/
/****** main()                                                           ******/
/******************************************************************************/
/****** Brief:  Main Program Endless Loop                                ******/
/******************************************************************************/
/****** Param:  -                                                        ******/
/******************************************************************************/
/****** Return: Does not return                                          ******/
/******************************************************************************/
/******************************************************************************/
int main(void)
{
	initialise_communications();
	// Create the mutex that controls access to the OpenThread API
	CommsMutexHandle = xSemaphoreCreateMutex();

	// Create the communications task. It controls the radio and the
	// Thread network stack.
	xTaskCreate(CommsTask, "Comms", 1024, NULL, 2, &CommsTaskHandle);

	xTaskCreate(ImageHandlerTask, "Image", 4 * 1024, NULL, 3, NULL);
	// Start the FreeRTOS Scheduler
	vTaskStartScheduler();

	// Should never get here
	for (;;)
	{
	}
}

int32_t SH_Return(int32_t n32In_R0, int32_t n32In_R1, int32_t *pn32Out_R0)
{
	(void)n32In_R0;
	(void)n32In_R1;
	(void)pn32Out_R0;

	return 0;
}

/* Stack overflow hook, required by FreeRTOS. */
void vApplicationStackOverflowHook(TaskHandle_t xTask, signed char *pcTaskName)
{
	/* Force an assert. */
	configASSERT(pcTaskName == 0);
}

/* configUSE_STATIC_ALLOCATION is set to 1, so the application must provide an
 * implementation of vApplicationGetIdleTaskMemory() to provide the memory that
 * is used by the Idle task. */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t ** ppxIdleTaskStackBuffer,
                                   uint32_t *     pulIdleTaskStackSize)
{
	/* If the buffers to be provided to the Idle task are declared inside this
	 * function then they must be declared static - otherwise they will be
	 * allocated on the stack and so not exists after this function exits. */
	static StaticTask_t xIdleTaskTCB;
	static StackType_t  uxIdleTaskStack[configMINIMAL_STACK_SIZE] __attribute__((aligned(32)));

	/* Pass out a pointer to the StaticTask_t structure in which the Idle
	 * task's state will be stored. */
	*ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

	/* Pass out the array that will be used as the Idle task's stack. */
	*ppxIdleTaskStackBuffer = uxIdleTaskStack;

	/* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
	 * Note that, as the array is necessarily of type StackType_t,
	 * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
	*pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/*-----------------------------------------------------------*/

/* configUSE_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
 * application must provide an implementation of vApplicationGetTimerTaskMemory()
 * to provide the memory that is used by the Timer service task. */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t ** ppxTimerTaskStackBuffer,
                                    uint32_t *     pulTimerTaskStackSize)
{
	/* If the buffers to be provided to the Timer task are declared inside this
	 * function then they must be declared static - otherwise they will be
	 * allocated on the stack and so not exists after this function exits. */
	static StaticTask_t xTimerTaskTCB;
	static StackType_t  uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH] __attribute__((aligned(32)));

	/* Pass out a pointer to the StaticTask_t structure in which the Timer
	 * task's state will be stored. */
	*ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

	/* Pass out the array that will be used as the Timer task's stack. */
	*ppxTimerTaskStackBuffer = uxTimerTaskStack;

	/* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
	 * Note that, as the array is necessarily of type StackType_t,
	 * configTIMER_TASK_STACK_DEPTH is specified in words, not bytes. */
	*pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
