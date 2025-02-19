/*
 *  Copyright (c) 2019, Cascoda Ltd.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>

#include "M2351.h"
#include "eadc.h"
#include "fmc.h"
#include "gpio.h"
#include "spi.h"
#include "sys.h"
#include "timer.h"
#include "usbd.h"

#include "cascoda_chili.h"

#include "cascoda-bm/cascoda_interface.h"
#include "cascoda-bm/cascoda_types.h"
#include "cascoda-bm/cascoda_usbhid.h"
#include "cascoda_chili_config.h"
#include "cascoda_chili_gpio.h"
#include "cascoda_chili_usb.h"
#include "cascoda_secure.h"

/* define system configuaration mask */
#define CLKCFG_ENPLL 0x01
#define CLKCFG_ENHXT 0x02
#define CLKCFG_ENHIRC 0x04
#define CLKCFG_ENHIRC48 0x08

#ifndef CUSTOM_PARTITION_H //Should be defined in custom partition file for nonsecure part
#error Custom partition_M2351.h not properly configured. This error exists to catch a common \
       misconfiguration problem - make sure the custom partition_M2351.h is being correctly \
       included. This should define the CUSTOM_PARTITION_H preprocessor macro.
#endif

static volatile u8_t asleep = 0;
static volatile u8_t wakeup = 0;
static u8_t          lxt_connected;
static uint32_t      systick_freq = 0;

__NONSECURE_ENTRY ca_error BSP_SetBootMode(sysreset_mode bootMode)
{
	uint32_t config[4];
	ca_error status = CA_ERROR_SUCCESS;

	/**
	 * Here we set bit 7 (BS) of CONFIG0 to set the default
	 * boot source of the chip. CONFIG0 is nonvolatile, but
	 * BS can be overridden by FMC_ISPCTL_BS when software
	 * reboot takes place.
	 */

	SYS_UnlockReg();
	FMC_Open();
	FMC_ENABLE_AP_UPDATE();
	FMC_ENABLE_CFG_UPDATE();

	FMC_ReadConfig(config, 4);
	if (bootMode == SYSRESET_DFU)
	{
		if (!(config[0] & (1 << 7)))
			goto exit;
		//Setting bit to 0, therefore can just overwrite
		config[0] &= ~(1 << 7); //LDROM
	}
	else if (bootMode == SYSRESET_APROM)
	{
		if (config[0] & (1 << 7))
			goto exit;
		//Setting bit to 1, therefore must erase
		config[0] |= (1 << 7); //APROM
		FMC_Erase(FMC_USER_CONFIG_0);
		FMC_Write(FMC_USER_CONFIG_1, config[1]);
		FMC_Write(FMC_USER_CONFIG_2, config[2]);
		FMC_Write(FMC_USER_CONFIG_3, config[3]);
	}
	else
	{
		status = CA_ERROR_NOT_HANDLED;
		goto exit;
	}

	FMC_Write(FMC_USER_CONFIG_0, config[0]);

exit:
	FMC_DISABLE_CFG_UPDATE();
	FMC_DISABLE_AP_UPDATE();
	FMC_Close();
	SYS_LockReg();
	return status;
}

__NONSECURE_ENTRY void CHILI_InitADC(u32_t reference)
{
	uint32_t clkFreqMhz = (uint32_t)CHILI_GetSystemFrequency();

	/* enable references */
	SYS_UnlockReg();
	SYS->VREFCTL = (SYS->VREFCTL & ~SYS_VREFCTL_VREFCTL_Msk) | reference;
	SYS_LockReg();

	/* enable EADC module clock */
	CLK_EnableModuleClock(EADC_MODULE);

	/* EADC clock source is PCLK->HCLK, divide down to 1 MHz */
	CLK_SetModuleClock(EADC_MODULE, 0, CLK_CLKDIV0_EADC(clkFreqMhz));

	/* reset EADC module */
	SYS_ResetModule(EADC_RST);
}

__NONSECURE_ENTRY void CHILI_DeinitADC()
{
	CLK_DisableModuleClock(EADC_MODULE);
}

__NONSECURE_ENTRY void CHILI_GPIOInitClock()
{
	CLK->IOPDCTL = 0x00000001;
}

__NONSECURE_ENTRY void CHILI_SetClockExternalCFGXT1(u8_t clk_external)
{
	const uint32_t DATA_FLASH_CONFIG0_CFGXT1_MASK = 0x08000000; /* mask for CONFIG0 CFGXT1 bit */
	uint32_t       cfg0;

	SYS_UnlockReg();
	FMC_Open();
	cfg0 = FMC_Read(FMC_USER_CONFIG_0);
	if (clk_external)
	{
		if (cfg0 & DATA_FLASH_CONFIG0_CFGXT1_MASK)
		{
			FMC_ENABLE_CFG_UPDATE();
			FMC_Write(FMC_USER_CONFIG_0, cfg0 & ~DATA_FLASH_CONFIG0_CFGXT1_MASK);
			FMC_DISABLE_CFG_UPDATE();
		}
	}
	else
	{
		if (!(cfg0 & DATA_FLASH_CONFIG0_CFGXT1_MASK))
		{
			FMC_ENABLE_CFG_UPDATE();
			FMC_Erase(FMC_CONFIG_BASE);
			FMC_Write(FMC_USER_CONFIG_0, cfg0 | DATA_FLASH_CONFIG0_CFGXT1_MASK);
			FMC_DISABLE_CFG_UPDATE();
		}
	}
	FMC_Close();
	SYS_LockReg();
}

u8_t CHILI_GetClockConfigMask(fsys_mhz fsys, u8_t enable_comms)
{
	u8_t mask      = 0;
	bool useExtClk = CHILI_GetUseExternalClock();

	/* check if PLL required */
	if (useExtClk)
	{
		if (fsys > FSYS_4MHZ) /* PLL needed to generate fsys from HXT */
			mask |= CLKCFG_ENPLL;
#ifdef USE_USB
		if (enable_comms) /* PLL needed to USB Clock */
			mask |= CLKCFG_ENPLL;
#endif
	}
	else
	{
		if ((fsys == FSYS_32MHZ) || (fsys == FSYS_64MHZ)) /* PLL needed to generate fsys from HIRC */
			mask |= CLKCFG_ENPLL;
	}
	if (enable_comms)
	{
#ifdef USE_UART
		if (UART_BAUDRATE > 115200) /* PLL needed to generate UART clock */
			mask |= CLKCFG_ENPLL;
#endif
	}

	/* check if HXT or HIRC required */
	if (useExtClk)
		mask |= CLKCFG_ENHXT; /* external clock always requires HXT */
	else
		mask |= CLKCFG_ENHIRC; /* internal clock always requires HIRC for timers etc. */

	/* check if HIRC48 required */
	if (fsys == FSYS_64MHZ) /* if 64MHz HIRC48 is needed for USB clock */
	{
#ifdef USE_USB
		if (enable_comms)
			mask |= CLKCFG_ENHIRC48;
#endif
	}
	if (!useExtClk) /* internal clock */
	{
#ifdef USE_USB
		if (enable_comms) /* HIRC48 always used for USB clock */
			mask |= CLKCFG_ENHIRC48;
#endif
		if (!(mask & CLKCFG_ENPLL))
		{
			if (fsys > FSYS_12MHZ) /* HIRC48 needed to generate fsys from HIRC48 */
				mask |= CLKCFG_ENHIRC48;
		}
	}

	return (mask);
}

static ca_error clk_enable(uint32_t pwrctlMask, uint32_t statusMask)
{
	ca_error status   = CA_ERROR_SUCCESS;
	uint32_t delayCnt = 0;

	SYS_UnlockReg();
	CLK->PWRCTL |= pwrctlMask;
	SYS_LockReg();
	for (delayCnt = 0; delayCnt < MAX_CLOCK_SWITCH_DELAY; ++delayCnt)
	{
		if (CLK->STATUS & statusMask)
			break;
	}
	if (delayCnt >= MAX_CLOCK_SWITCH_DELAY)
	{
		status = CA_ERROR_FAIL;
	}
	return (status);
}

__NONSECURE_ENTRY ca_error CHILI_ClockInit(fsys_mhz fsys, u8_t enable_comms)
{
	uint32_t delayCnt;
	ca_error status = CA_ERROR_SUCCESS;
	u8_t     clkcfg;
	uint32_t divisor = 1;

	switch (fsys)
	{
	case FSYS_4MHZ:
		divisor = 12;
		break;
	case FSYS_12MHZ:
		divisor = 4;
		break;
	case FSYS_16MHZ:
	case FSYS_32MHZ: //For 32MHz a 96MHz PLL is used, hence the out-of-sequence divisor
		divisor = 3;
		break;
	case FSYS_24MHZ:
		divisor = 2;
		break;
	case FSYS_48MHZ:
	case FSYS_64MHZ:
		divisor = 1;
		break;
	}

	/* NOTE: __HXT in system_M2351.h has to be changed to correct input clock frequency !! */
#if __HXT != 4000000UL
#error "__HXT Not set correctly in system_M2351.h"
#endif

	/* Oscillator sources:
	 *	- HXT 		 4 MHz
	 *	- HIRC		12 MHz
	 *	- HIRC48	48 MHz */

	clkcfg = CHILI_GetClockConfigMask(fsys, enable_comms);

	/* enable HXT */
	if (clkcfg & CLKCFG_ENHXT)
	{
		status =
		    clk_enable((CLK_PWRCTL_HXTEN_Msk | CLK_PWRCTL_LXTEN_Msk | CLK_PWRCTL_LIRCEN_Msk), CLK_STATUS_HXTSTB_Msk);
		if (status)
		{
			ca_log_crit("HXT Enable Fail");
			return (status);
		}
	}

	/* enable HIRC */
	if (clkcfg & CLKCFG_ENHIRC)
	{
		status =
		    clk_enable((CLK_PWRCTL_HIRCEN_Msk | CLK_PWRCTL_LXTEN_Msk | CLK_PWRCTL_LIRCEN_Msk), CLK_STATUS_HIRCSTB_Msk);
		if (status)
		{
			ca_log_crit("HIRC Enable Fail");
			return (status);
		}
	}

	/* enable HIRC48 */
	if (clkcfg & CLKCFG_ENHIRC48)
	{
		status = clk_enable((CLK_PWRCTL_HIRC48EN_Msk | CLK_PWRCTL_LXTEN_Msk | CLK_PWRCTL_LIRCEN_Msk),
		                    CLK_STATUS_HIRC48STB_Msk);
		if (status)
		{
			ca_log_crit("HIRC48 Enable Fail");
			return (status);
		}
	}

	/* enable PLL */
	if (clkcfg & CLKCFG_ENPLL)
	{
		/*
		*****************************************************************************************
		PLL Setup
		*****************************************************************************************
		FREF =  2 -   8 MHz
		FVCO = 96 - 200 MHz
		FOUT = 24 - 144 MHz
		*****************************************************************************************
		         FIN FREF FVCO FOUT  NR NF NO INDIV FBDIV OUTDIV   PLLCTL
		*****************************************************************************************
		FOUT = 48 MHz:
		HIRC      12    4   96   48   3 12  2     2    10      1   0x0008440A
		HXT        4    4   96   48   1 12  2     0    10      1   0x0000400A
		FOUT = 96 MHz (for 32MHz HCLK)
		HIRC      12    4   96   48   3 12  1     2    10      0   0x0008040A
		HXT        4    4   96   48   1 12  1     0    10      0   0x0000000A
		FOUT = 64 MHz:(for 64MHz HCLK)
		HIRC      12    4  128   64   3 16  2     2    14      1   0x0008440E
		HXT        4    4  128   64   1 16  2     0    14      1   0x0000400E
		*****************************************************************************************
		Note: Using CLK_EnablePLL() does not always give correct results, so avoid!
		*/
		SYS_UnlockReg();
		if (clkcfg & CLKCFG_ENHXT)
		{
			if (fsys == FSYS_64MHZ)
				CLK->PLLCTL = 0x0000400E;
			else if (fsys == FSYS_32MHZ)
				CLK->PLLCTL = 0x0000000A;
			else
				CLK->PLLCTL = 0x0000400A;
		}
		else
		{
			if (fsys == FSYS_64MHZ)
				CLK->PLLCTL = 0x0008440E;
			else if (fsys == FSYS_32MHZ)
				CLK->PLLCTL = 0x0008040A;
			else
				CLK->PLLCTL = 0x0008440A;
		}
		SYS_LockReg();
		for (delayCnt = 0; delayCnt < MAX_CLOCK_SWITCH_DELAY; delayCnt++)
		{
			if (CLK->STATUS & CLK_STATUS_PLLSTB_Msk)
				break;
		}
		if (delayCnt >= MAX_CLOCK_SWITCH_DELAY)
		{
			status = CA_ERROR_FAIL;
			ca_log_crit("PLL Enable Fail");
			return (status);
		}
	}

	/* set system clock */
	SYS_UnlockReg();
	if (clkcfg & CLKCFG_ENHXT)
	{
		if (fsys == FSYS_4MHZ)
			CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HXT, CLK_CLKDIV0_HCLK(1));
		else
			CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_PLL, CLK_CLKDIV0_HCLK(divisor));
	}
	else
	{
		if ((clkcfg & CLKCFG_ENHIRC) && (fsys == FSYS_4MHZ))
			CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HIRC, CLK_CLKDIV0_HCLK(3));
		else if ((clkcfg & CLKCFG_ENHIRC) && (fsys == FSYS_12MHZ))
			CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HIRC, CLK_CLKDIV0_HCLK(1));
		else if (clkcfg & CLKCFG_ENPLL)
		{
			CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_PLL, CLK_CLKDIV0_HCLK(divisor));
		}
		else if (clkcfg & CLKCFG_ENHIRC48)
		{
			CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HIRC48, CLK_CLKDIV0_HCLK(divisor));
		}
	}

#ifdef USE_USB
	/* set USB clock */
	SYS_UnlockReg();
	if (enable_comms)
	{
		if (clkcfg & CLKCFG_ENHIRC48)
		{
			CLK_SetModuleClock(USBD_MODULE, CLK_CLKSEL0_USBSEL_HIRC48, CLK_CLKDIV0_USB(1));
		}
		else
		{
			if (fsys == FSYS_32MHZ)
				CLK_SetModuleClock(USBD_MODULE, CLK_CLKSEL0_USBSEL_PLL, CLK_CLKDIV0_USB(2));
			else
				CLK_SetModuleClock(USBD_MODULE, CLK_CLKSEL0_USBSEL_PLL, CLK_CLKDIV0_USB(1));
		}
		CLK_EnableModuleClock(USBD_MODULE);
	}
	else
	{
		CLK_DisableModuleClock(USBD_MODULE);
	}
	SYS_LockReg();
#endif

	/* Set HCLK back to HIRC if clock switching error happens */
	if (CLK->STATUS & CLK_STATUS_CLKSFAIL_Msk)
	{
		SYS_UnlockReg();
		CLK->PWRCTL |= CLK_PWRCTL_HIRCEN_Msk;
		CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HIRC, CLK_CLKDIV0_HCLK(1));
		SYS_LockReg();
		ca_log_warn("Clock Switch Fail, Restarting with 12MHz");
		status = CA_ERROR_FAIL;
	}

	CHILI_SetSysTickFreq(systick_freq);

	return (status);
}

__NONSECURE_ENTRY void CHILI_SetSysTickFreq(uint32_t freqHz)
{
	systick_freq = freqHz;

	SysTick->CTRL = 0;
	SysTick->VAL  = 0;
	if (systick_freq)
	{
		/* Configure SysTick to interrupt at the requested rate. */
		uint32_t sysFreq = (uint32_t)CHILI_GetSystemFrequency() * 1000000;
		SysTick->LOAD    = (sysFreq / systick_freq) - 1UL;
		SysTick->CTRL    = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
	}
}

__NONSECURE_ENTRY void CHILI_CompleteClockInit(fsys_mhz fsys, u8_t enable_comms)
{
	u8_t clkcfg;

	clkcfg = CHILI_GetClockConfigMask(fsys, enable_comms);

	if (!(CLK->STATUS & CLK_STATUS_CLKSFAIL_Msk))
	{
		SYS_UnlockReg();
		if (!(clkcfg & CLKCFG_ENPLL)) /* disable PLL */
			CLK_DisablePLL();
		if (!(clkcfg & CLKCFG_ENHIRC48)) /* disable HIRC48 */
			CLK->PWRCTL &= ~(CLK_PWRCTL_HIRC48EN_Msk);
		if (!(clkcfg & CLKCFG_ENHIRC)) /* disable HIRC */
			CLK->PWRCTL &= ~(CLK_PWRCTL_HIRCEN_Msk);
		if (!(clkcfg & CLKCFG_ENHXT)) /* disable HXT */
			CLK->PWRCTL &= ~(CLK_PWRCTL_HXTEN_Msk);
		SYS_LockReg();
	}
	else
	{
		ca_log_crit("Clock Switch Fail");
	}
	CHILI_EnableIntOscCal();
}

__NONSECURE_ENTRY void CHILI_EnableIntOscCal(void)
{
	/* enable HIRC trimming */
	SYS->TISTS12M = 0xFFFF;
	if ((CLK->PWRCTL & CLK_PWRCTL_HIRCEN_Msk) && (CLK->STATUS & CLK_STATUS_LXTSTB_Msk))
	{
		/* trim HIRC using LXT */
		SYS->TCTL12M = 0x00F1;
		/* enable HIRC-trim interrupt */
		NVIC_EnableIRQ(CKFAIL_IRQn);
		SYS->TIEN12M = (SYS_TIEN12M_TFAILIEN_Msk | SYS_TIEN12M_CLKEIEN_Msk);
	}

	/* enable HIRC48 trimming */
	SYS->TISTS48M = 0xFFFF;
	if ((CLK->PWRCTL & CLK_PWRCTL_HIRC48EN_Msk) && (CLK->STATUS & CLK_STATUS_LXTSTB_Msk))
	{
		/* trim HIRC48 using LXT */
		SYS->TCTL48M = 0x00F1;
		/* enable HIRC48-trim interrupt */
		NVIC_EnableIRQ(CKFAIL_IRQn);
		SYS->TIEN48M = (SYS_TIEN48M_TFAILIEN_Msk | SYS_TIEN48M_CLKEIEN_Msk);
	}
}

__NONSECURE_ENTRY void CHILI_DisableIntOscCal(void)
{
	SYS->TISTS12M = 0xFFFF;
	SYS->TIEN12M  = 0;
	SYS->TCTL12M  = 0;

	SYS->TISTS48M = 0xFFFF;
	SYS->TIEN48M  = 0;
	SYS->TCTL48M  = 0;
}

__NONSECURE_ENTRY void CHILI_TimersInit(void)
{
	bool useExtClk = CHILI_GetUseExternalClock();

	/* TIMER0: millisecond periodic tick (AbsoluteTicks) and power-down wake-up */
	/* TIMER1: microsecond timer / counter */
	CLK_EnableModuleClock(TMR0_MODULE);
	CLK_EnableModuleClock(TMR1_MODULE);

	/* configure clock selects and dividers for timers 0 and 1 */
	/* clocks are fixed to HXT (4 MHz) or HIRC (12 MHz) */
	if (useExtClk)
	{
		CLK_SetModuleClock(TMR0_MODULE, CLK_CLKSEL1_TMR0SEL_HXT, 0);
		CLK_SetModuleClock(TMR1_MODULE, CLK_CLKSEL1_TMR1SEL_HXT, 0);
	}
	else
	{
		CLK_SetModuleClock(TMR0_MODULE, CLK_CLKSEL1_TMR0SEL_HIRC, 0);
		CLK_SetModuleClock(TMR1_MODULE, CLK_CLKSEL1_TMR1SEL_HIRC, 0);
	}

	TIMER_Open(TIMER0, TIMER_PERIODIC_MODE, 1000000);
	TIMER_Open(TIMER1, TIMER_CONTINUOUS_MODE, 1000000);

	/* prescale value has to be set after TIMER_Open() is called! */
	if (useExtClk)
	{
		/*  4 MHZ Clock: prescaler 3 gives 1 uSec units */
		TIMER_SET_PRESCALE_VALUE(TIMER0, 3);
		/*  4 MHZ Clock: prescalar 3 gives 1 uSec units */
		TIMER_SET_PRESCALE_VALUE(TIMER1, 3);
	}
	else
	{
		/* 12 MHZ Clock: prescaler 11 gives 1 uSec units */
		TIMER_SET_PRESCALE_VALUE(TIMER0, 11);
		/* 12 MHZ Clock: prescalar 11 gives 1 uSec units */
		TIMER_SET_PRESCALE_VALUE(TIMER1, 11);
	}

	/* note timers are 24-bit (+ 8-bit prescale) */
	/* 1uSec units, so 1000 is 1ms */
	TIMER_SET_CMP_VALUE(TIMER0, 1000);
	/* 1uSec units, counts microseconds */
	TIMER_SET_CMP_VALUE(TIMER1, 0xFFFFFF);

	NVIC_EnableIRQ(TMR0_IRQn);
	TIMER_EnableInt(TIMER0);
	TIMER_Start(TIMER0);

	/* For some reason, this is what is needed to kick the linker into actually linking the ISR file */
	TMR0_IRQHandler();
}

__NONSECURE_ENTRY void CHILI_PDMAInit(void)
{
	SYS_UnlockReg();
	CLK_EnableModuleClock(PDMA0_MODULE);
	SYS_ResetModule(PDMA0_RST);
	SYS_LockReg();
	NVIC_EnableIRQ(PDMA0_IRQn);
}

__NONSECURE_ENTRY void CHILI_ReInitSetTimerPriority()
{
	NVIC_SetPriority(TMR0_IRQn, 0);
	NVIC_SetPriority(TMR1_IRQn, 0);
}

int32_t SH_Return(int32_t n32In_R0, int32_t n32In_R1, int32_t *pn32Out_R0)
{
	(void)n32In_R0;
	(void)n32In_R1;
	(void)pn32Out_R0;

	return 0;
}

__NONSECURE_ENTRY uint32_t HardFault_Handler(uint32_t lr, uint32_t msp, uint32_t psp)
{
	(void)lr;
	(void)msp;
	(void)psp;

#ifdef CASCODA_HF_REBOOT
	BSP_SystemReset(SYSRESET_APROM);
#endif
	while (1)
		;

	return 1;
}

__NONSECURE_ENTRY void ProcessHardFault()
{
#ifdef CASCODA_HF_REBOOT
	BSP_SystemReset(SYSRESET_APROM);
#endif
	while (1)
		;
}

__NONSECURE_ENTRY void CHILI_CRYPTOEnableClock(void)
{
	CLK_EnableModuleClock(CRPT_MODULE);
}

__NONSECURE_ENTRY void CHILI_CRYPTODisableClock(void)
{
	CLK_DisableModuleClock(CRPT_MODULE);
}

__NONSECURE_ENTRY void CHILI_ModuleSetMFP(enPortnum portnum, u8_t portbit, u8_t func)
{
	u32_t mask, data;
	if (portbit < 8)
	{
		mask = (0x7 << 4 * portbit);
		data = (func << 4 * portbit);
		if (portnum == PN_A)
			SYS->GPA_MFPL = (SYS->GPA_MFPL & (~mask)) | data;
		else if (portnum == PN_B)
			SYS->GPB_MFPL = (SYS->GPB_MFPL & (~mask)) | data;
		else if (portnum == PN_C)
			SYS->GPC_MFPL = (SYS->GPC_MFPL & (~mask)) | data;
		else if (portnum == PN_D)
			SYS->GPD_MFPL = (SYS->GPD_MFPL & (~mask)) | data;
		else if (portnum == PN_E)
			SYS->GPE_MFPL = (SYS->GPE_MFPL & (~mask)) | data;
		else if (portnum == PN_F)
			SYS->GPF_MFPL = (SYS->GPF_MFPL & (~mask)) | data;
		else if (portnum == PN_F)
			SYS->GPG_MFPL = (SYS->GPG_MFPL & (~mask)) | data;
		else if (portnum == PN_F)
			SYS->GPH_MFPL = (SYS->GPH_MFPL & (~mask)) | data;
	}
	else if (portbit < 16)
	{
		mask = (0x7 << 4 * (portbit - 8));
		data = (func << 4 * (portbit - 8));
		if (portnum == PN_A)
			SYS->GPA_MFPH = (SYS->GPA_MFPH & (~mask)) | data;
		else if (portnum == PN_B)
			SYS->GPB_MFPH = (SYS->GPB_MFPH & (~mask)) | data;
		else if (portnum == PN_C)
			SYS->GPC_MFPH = (SYS->GPC_MFPH & (~mask)) | data;
		else if (portnum == PN_D)
			SYS->GPD_MFPH = (SYS->GPD_MFPH & (~mask)) | data;
		else if (portnum == PN_E)
			SYS->GPE_MFPH = (SYS->GPE_MFPH & (~mask)) | data;
		else if (portnum == PN_F)
			SYS->GPF_MFPH = (SYS->GPF_MFPH & (~mask)) | data;
		else if (portnum == PN_F)
			SYS->GPG_MFPH = (SYS->GPG_MFPH & (~mask)) | data;
		else if (portnum == PN_F)
			SYS->GPH_MFPH = (SYS->GPH_MFPH & (~mask)) | data;
		/* no PF_H_MFP */
	}
}

#if defined(USE_UART)

/* Local Functions */
static void CHILI_UARTDMAInitialise(void);

__NONSECURE_ENTRY void CHILI_UARTInit(void)
{
	if (UART_BAUDRATE <= 115200)
	{
		// 4 MHz
		if (CHILI_GetUseExternalClock())
			CLK_SetModuleClock(UART_MODULE, UART_CLK_HXT, UART_CLKDIV(1));
		else
			CLK_SetModuleClock(UART_MODULE, UART_CLK_HIRC, UART_CLKDIV(3));
	}
	else
	{
		// 48 MHz
		if ((CHILI_GetSystemFrequency() == FSYS_32MHZ) || (CHILI_GetSystemFrequency() == FSYS_64MHZ))
			CLK_SetModuleClock(UART_MODULE, UART_CLK_PLL, UART_CLKDIV(2));
		else
			CLK_SetModuleClock(UART_MODULE, UART_CLK_PLL, UART_CLKDIV(1));
	}

	CLK_EnableModuleClock(UART_MODULE);

	/* Initialise UART */
	SYS_ResetModule(UART_RST);
	UART_SetLineConfig(UART, UART_BAUDRATE, UART_WORD_LEN_8, UART_PARITY_NONE, UART_STOP_BIT_1);
	UART_Open(UART, UART_BAUDRATE);
	UART_EnableInt(UART,
	               UART_INTEN_RDAIEN_Msk); //TODO: This is probably one of the reasons UART doesn't work on tz
#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
	TZ_NVIC_EnableIRQ_NS(UART_IRQn);
#else
	NVIC_EnableIRQ(UART_IRQn);
#endif

	CHILI_ModuleSetMFP(UART_TXD_PNUM, UART_TXD_PIN, PMFP_UART);
	CHILI_ModuleSetMFP(UART_RXD_PNUM, UART_RXD_PIN, PMFP_UART);

	/* enable DMA */
	CHILI_UARTDMAInitialise();
}

__NONSECURE_ENTRY void CHILI_UARTDeinit(void)
{
#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
	TZ_NVIC_DisableIRQ_NS(UART_IRQn);
#else
	NVIC_DisableIRQ(UART_IRQn);
#endif
	UART_DisableInt(UART, UART_INTEN_RDAIEN_Msk);
	CLK_DisableModuleClock(UART_MODULE);
	UART_Close(UART);
	CHILI_ModuleSetMFP(UART_TXD_PNUM, UART_TXD_PIN, PMFP_GPIO);
	CHILI_ModuleSetMFP(UART_RXD_PNUM, UART_RXD_PIN, PMFP_GPIO);
	GPIO_SetPullCtl(UART_TXD_PORT, BITMASK(UART_TXD_PIN), GPIO_PUSEL_PULL_UP);
	GPIO_SetPullCtl(UART_RXD_PORT, BITMASK(UART_RXD_PIN), GPIO_PUSEL_PULL_UP);
}

/******************************************************************************/
/***************************************************************************/ /**
 * \brief Initialises DMA access for UART
 *******************************************************************************
 ******************************************************************************/
static void CHILI_UARTDMAInitialise(void)
{
	/* Open DMA channels */
	PDMA_Open(PDMA0, (1 << UART_RX_DMA_CH) | (1 << UART_TX_DMA_CH));

	/* Single request type */
	PDMA_SetBurstType(PDMA0, UART_TX_DMA_CH, PDMA_REQ_SINGLE, 0);
	PDMA_SetBurstType(PDMA0, UART_RX_DMA_CH, PDMA_REQ_SINGLE, 0);

	/* Disable table interrupt */
	PDMA0->DSCT[UART_TX_DMA_CH].CTL |= PDMA_DSCT_CTL_TBINTDIS_Msk;
	PDMA0->DSCT[UART_RX_DMA_CH].CTL |= PDMA_DSCT_CTL_TBINTDIS_Msk;
}

#endif // USE_UART

/******************************************************************************/
/****** HID Mode                                                         ******/
/******************************************************************************/
/* Define EP maximum packet size */
#define EP0_MAX_PKT_SIZE HID_CTRL_MAX_SIZE
#define EP1_MAX_PKT_SIZE EP0_MAX_PKT_SIZE
#define EP2_MAX_PKT_SIZE HID_FRAGMENT_SIZE
#define EP3_MAX_PKT_SIZE HID_FRAGMENT_SIZE

#define SETUP_BUF_BASE 0
#define SETUP_BUF_LEN 8
#define EP0_BUF_BASE (SETUP_BUF_BASE + SETUP_BUF_LEN)
#define EP0_BUF_LEN EP0_MAX_PKT_SIZE
#define EP1_BUF_BASE (SETUP_BUF_BASE + SETUP_BUF_LEN)
#define EP1_BUF_LEN EP1_MAX_PKT_SIZE
#define EP2_BUF_BASE (EP1_BUF_BASE + EP1_BUF_LEN)
#define EP2_BUF_LEN EP2_MAX_PKT_SIZE
#define EP3_BUF_BASE (EP2_BUF_BASE + EP2_BUF_LEN)
#define EP3_BUF_LEN EP3_MAX_PKT_SIZE

#define TBUFFS (8) /* Number of HID transmit buffers */
#define RBUFFS (8) /* Number of HID receive  buffers */

#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3L)
//Duplicate ca_log for secure code
void ca_log(ca_loglevel loglevel, const char *format, va_list argp)
{
	char *lev_str;

	switch (loglevel)
	{
	case CA_LOGLEVEL_CRIT:
		lev_str = "CRIT: ";
		break;
	case CA_LOGLEVEL_WARN:
		lev_str = "WARN: ";
		break;
	case CA_LOGLEVEL_NOTE:
		lev_str = "NOTE: ";
		break;
	case CA_LOGLEVEL_INFO:
		lev_str = "INFO: ";
		break;
	case CA_LOGLEVEL_DEBG:
		lev_str = "DEBG: ";
		break;
	default:
		lev_str = "UNKN: ";
		break;
	}

	printf("%dms %s", BSP_ReadAbsoluteTime(), lev_str);
	vprintf(format, argp);
	printf("\r\n");
}
#endif

__NONSECURE_ENTRY void CHILI_EnableTemperatureSensor()
{
	SYS_UnlockReg();
	SYS->IVSCTL |= SYS_IVSCTL_VTEMPEN_Msk;
	SYS_LockReg();
}

__NONSECURE_ENTRY void CHILI_DisableTemperatureSensor()
{
	SYS_UnlockReg();
	SYS->IVSCTL &= ~SYS_IVSCTL_VTEMPEN_Msk;
	SYS_LockReg();
}

__NONSECURE_ENTRY void CHILI_PowerDownSelectClock(u8_t use_timer0)
{
	lxt_connected = 0;
	/* use LF 32.768 kHz Crystal LXT for timer if present, switch off if not */
	if ((CLK->STATUS & CLK_STATUS_LXTSTB_Msk) && use_timer0)
	{
		lxt_connected = 1;
		/* Note: LIRC cannot be powered down as used for GPIOs */
	}
	else
	{
		lxt_connected = 0;
		SYS_UnlockReg();
		CLK->PWRCTL &= ~(CLK_PWRCTL_LXTEN_Msk);
		SYS_LockReg();
	}
}

__NONSECURE_ENTRY void CHILI_PowerDownSecure(u32_t sleeptime_ms, u8_t use_timer0, u8_t dpd)
{
	TIMER_Stop(TIMER0);
	SYS_ResetModule(TMR0_RST);
	TIMER_ResetCounter(TIMER0);

	if (use_timer0)
	{
		/* timer has to be set up otherwise wakeup is unreliable when not in dpd */
		CLK_EnableModuleClock(TMR0_MODULE);
		if (lxt_connected)
			CLK_SetModuleClock(TMR0_MODULE, CLK_CLKSEL1_TMR0SEL_LXT, 0);
		else
			CLK_SetModuleClock(TMR0_MODULE, CLK_CLKSEL1_TMR0SEL_LIRC, 0);

		/*  1s period - 1Hz */
		TIMER_Open(TIMER0, TIMER_ONESHOT_MODE, 1);

		if (lxt_connected)
		{
			if (sleeptime_ms < 1000)
			{
				/* 32.768 kHz Clock: prescaler 32 gives roughly 1 ms units */
				TIMER_SET_PRESCALE_VALUE(TIMER0, 32);
				TIMER_SET_CMP_VALUE(TIMER0, sleeptime_ms);
			}
			else
			{
				/* 32.768 kHz Clock: prescaler 255 gives 7.8125 ms units, so 128 is 1 sec */
				TIMER_SET_PRESCALE_VALUE(TIMER0, 255);
				TIMER_SET_CMP_VALUE(TIMER0, (128 * sleeptime_ms) / 1000);
			}
		}
		else
		{
			/*  10 kHz Clock: prescaler 9 gives 1 ms units */
			TIMER_SET_PRESCALE_VALUE(TIMER0, 9);
			TIMER_SET_CMP_VALUE(TIMER0, sleeptime_ms);
		}

		NVIC_EnableIRQ(TMR0_IRQn);
		TIMER_EnableInt(TIMER0);
		TIMER_EnableWakeup(TIMER0);
		TIMER_Start(TIMER0);
	}

	SYS_UnlockReg();

	/* disable peripheral memory (fmc, pdma0/1, usbd, can) */
	SYS->SRAMPPCT = 0x000002AA;

	if (dpd)
	{
		CLK->PWRCTL &= ~(CLK_PWRCTL_HXTEN_Msk); /* turn off all clocks */
		CLK->PWRCTL &= ~(CLK_PWRCTL_LXTEN_Msk);
		CLK->PWRCTL &= ~(CLK_PWRCTL_LIRCEN_Msk);
		CLK->PWRCTL &= ~(CLK_PWRCTL_HIRCEN_Msk);
		CLK->PWRCTL &= ~(CLK_PWRCTL_HIRC48EN_Msk);
		CLK_ENABLE_RTCWK();
		if ((CASCODA_CHILI2_CONFIG == 1) && (VBUS_CONNECTED_PVAL == 1))
		{
			/* Enable SPD wakeup from pin */
			CLK_EnableSPDWKPin(2, 0, CLK_SPDWKPIN_FALLING, CLK_SPDWKPIN_DEBOUNCEDIS);
			/* SPD, no data retention, from reset on wake-up */
			CLK_SetPowerDownMode(CLK_PMUCTL_PDMSEL_SPD);
		}
		else
		{
			/* Enable DPD wakeup from pin */
			CLK_EnableDPDWKPin(CLK_DPDWKPIN_FALLING);
			/* DPD, no data retention, from reset on wake-up */
			CLK_SetPowerDownMode(CLK_PMUCTL_PDMSEL_DPD);
		}
	}
	else
	{
		/* ULLPD, data retention, program continuation on wake-up */
		CLK_SetPowerDownMode(CLK_PMUCTL_PDMSEL_ULLPD); /* ULLPD */
	}

	CHILI_SetWakeup(0);

	do
	{
		CLK->PWRCTL |= CLK_PWRCTL_PDEN_Msk; /* Set power down bit */
		SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;  /* Sleep Deep */
		__WFI();                            /* really enter power down here !!! */

		__DSB();
		__ISB();
	} while (!CHILI_GetWakeup());

	/* re-enable peripheral memory */
	SYS->SRAMPPCT = 0x00000000;

	SYS_LockReg();

	if (USE_WATCHDOG_POWEROFF)
		BSP_RTCDisableAlarm();

	TIMER_Stop(TIMER0);
	SYS_ResetModule(TMR0_RST);

	GPIO_SET_DEBOUNCE_TIME(PA, GPIO_DBCTL_DBCLKSRC_HCLK, GPIO_DBCTL_DBCLKSEL_8);
	GPIO_SET_DEBOUNCE_TIME(PB, GPIO_DBCTL_DBCLKSRC_HCLK, GPIO_DBCTL_DBCLKSEL_8);
	GPIO_SET_DEBOUNCE_TIME(PC, GPIO_DBCTL_DBCLKSRC_HCLK, GPIO_DBCTL_DBCLKSEL_8);
	GPIO_SET_DEBOUNCE_TIME(PD, GPIO_DBCTL_DBCLKSRC_HCLK, GPIO_DBCTL_DBCLKSEL_8);
	GPIO_SET_DEBOUNCE_TIME(PE, GPIO_DBCTL_DBCLKSRC_HCLK, GPIO_DBCTL_DBCLKSEL_8);
	GPIO_SET_DEBOUNCE_TIME(PF, GPIO_DBCTL_DBCLKSRC_HCLK, GPIO_DBCTL_DBCLKSEL_8);
	GPIO_SET_DEBOUNCE_TIME(PG, GPIO_DBCTL_DBCLKSRC_HCLK, GPIO_DBCTL_DBCLKSEL_8);
	GPIO_SET_DEBOUNCE_TIME(PH, GPIO_DBCTL_DBCLKSRC_HCLK, GPIO_DBCTL_DBCLKSEL_8);
}

__NONSECURE_ENTRY void CHILI_SetWakeup(u8_t new_wakeup)
{
	wakeup = new_wakeup;
}

__NONSECURE_ENTRY u8_t CHILI_GetWakeup()
{
	return wakeup;
}

__NONSECURE_ENTRY void CHILI_SetAsleep(u8_t new_asleep)
{
	asleep = new_asleep;
}

__NONSECURE_ENTRY u8_t CHILI_GetAsleep()
{
	return asleep;
}

/** Wait until system is stable after potential usb plug-in */
__NONSECURE_ENTRY void CHILI_WaitForSystemStable(void)
{
	while (!(CLK->STATUS & CLK_STATUS_HIRCSTB_Msk))
		;
}

/*---------------------------------------------------------------------------*
 * See cascoda-bm/cascoda_interface.h for docs                               *
 *---------------------------------------------------------------------------*/
__NONSECURE_ENTRY void BSP_WaitUs(u32_t us)
{
	volatile u32_t t1;
	volatile u32_t t2;

	/* Note this is dependent on the TIMER0 prescaling in CHILI_TimersInit() */
	/* as it is pre-scaled to 1 us and counting to 1 ms, max. wait time is 1000 us */
	if (us >= 1000)
		return;
	t1 = TIMER_GetCounter(TIMER0);
	do
	{
		t2 = TIMER_GetCounter(TIMER0);
		if (t1 > t2)
			t2 += 1000;
	} while (t2 - t1 < us);
}

__NONSECURE_ENTRY void CHILI_EnableTRNGClk(void)
{
	SYS_UnlockReg();
	CLK_EnableModuleClock(TRNG_MODULE);
	SYS_LockReg();
}

__NONSECURE_ENTRY void CHILI_DisableTRNGClk(void)
{
	SYS_UnlockReg();
	CLK_DisableModuleClock(TRNG_MODULE);
	SYS_LockReg();
}
