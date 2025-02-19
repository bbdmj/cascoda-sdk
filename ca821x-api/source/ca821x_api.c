/**
 * @file
 * @brief API Access Function Declarations for MCPS, MLME, HWME and TDME.
 */
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
/***************************************************************************/ /**
 * \def DATAREQ
 * Shorthand for MCPS-DATA Request parameter set
 * \def ASSOCREQ
 * Shorthand for MLME-ASSOCIATE Request parameter set
 * \def ASSOCRSP
 * Shorthand for MLME-ASSOCIATE Response parameter set
 * \def GETREQ
 * Shorthand for MLME-GET Request parameter set
 * \def GETCNF
 * Shorthand for MLME-GET Confirm parameter set
 * \def ORPHANRSP
 * Shorthand for MLME-ORPHAN Response parameter set
 *
 * Usually used for requests with a single-byte payload etc.
 * \def SIMPLECNF
 * Shorthand for the raw parameter data of a confirm
 *
 * Usually used for confirms with just a status etc.
 * \def SCANREQ
 * Shorthand for MLME-SCAN Request parameter set
 * \def SETREQ
 * Shorthand for MLME-SET Request parameter set
 * \def STARTREQ
 * Shorthand for MLME-START Request parameter set
 * \def POLLREQ
 * Shorthand for MLME-POLL Request parameter set
 ******************************************************************************/
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ca821x_api.h"
#include "ca821x_blacklist.h"
#include "ca821x_log.h"
#include "mac_messages.h"

/** LQI limit, below which received frames should be rejected */
#define API_LQI_LIMIT (75)

uint8_t ca821x_get_sync_response_id(uint8_t cmdid)
{
	uint8_t rval = 0;

	switch (cmdid)
	{
	case SPI_MCPS_PURGE_REQUEST:
		rval = SPI_MCPS_PURGE_CONFIRM;
		break;
	case SPI_MLME_GET_REQUEST:
		rval = SPI_MLME_GET_CONFIRM;
		break;
	case SPI_MLME_RESET_REQUEST:
		rval = SPI_MLME_RESET_CONFIRM;
		break;
	case SPI_MLME_RX_ENABLE_REQUEST:
		rval = SPI_MLME_RX_ENABLE_CONFIRM;
		break;
	case SPI_MLME_SET_REQUEST:
		rval = SPI_MLME_SET_CONFIRM;
		break;
	case SPI_MLME_START_REQUEST:
		rval = SPI_MLME_START_CONFIRM;
		break;
	case SPI_MLME_POLL_REQUEST:
		rval = SPI_MLME_POLL_CONFIRM;
		break;
	case SPI_HWME_SET_REQUEST:
		rval = SPI_HWME_SET_CONFIRM;
		break;
	case SPI_HWME_GET_REQUEST:
		rval = SPI_HWME_GET_CONFIRM;
		break;
	case SPI_HWME_HAES_REQUEST:
		rval = SPI_HWME_HAES_CONFIRM;
		break;
	case SPI_TDME_SETSFR_REQUEST:
		rval = SPI_TDME_SETSFR_CONFIRM;
		break;
	case SPI_TDME_GETSFR_REQUEST:
		rval = SPI_TDME_GETSFR_CONFIRM;
		break;
	case SPI_TDME_TESTMODE_REQUEST:
		rval = SPI_TDME_TESTMODE_CONFIRM;
		break;
	case SPI_TDME_SET_REQUEST:
		rval = SPI_TDME_SET_CONFIRM;
		break;
	case SPI_TDME_TXPKT_REQUEST:
		rval = SPI_TDME_TXPKT_CONFIRM;
		break;
	case SPI_TDME_LOTLK_REQUEST:
		rval = SPI_TDME_LOTLK_CONFIRM;
		break;
	}

	return rval;
}

ca_error ca821x_api_init(struct ca821x_dev *pDeviceRef)
{
	if (pDeviceRef == NULL)
		return CA_ERROR_INVALID_ARGS;

	memset(pDeviceRef, 0, sizeof(*pDeviceRef));

#if (CASCODA_CA_VER == 8210)
	pDeviceRef->shortaddr = 0xFFFF;
	pDeviceRef->lqi_mode  = HWME_LQIMODE_CS;
#endif

	return CA_ERROR_SUCCESS;
}

ca_mac_status MCPS_DATA_request(uint8_t            SrcAddrMode,
                                struct FullAddr    DstAddr,
                                uint8_t            MsduLength,
                                uint8_t *          pMsdu,
                                uint8_t            MsduHandle,
                                uint8_t            TxOptions,
                                struct SecSpec *   pSecurity,
                                struct ca821x_dev *pDeviceRef)
{
	struct SecSpec *   pSec;
	struct MAC_Message Command;
#define DATAREQ (Command.PData.DataReq)
	Command.CommandId   = SPI_MCPS_DATA_REQUEST;
	DATAREQ.SrcAddrMode = SrcAddrMode;
	DATAREQ.Dst         = DstAddr;
	DATAREQ.MsduLength  = MsduLength;
	DATAREQ.MsduHandle  = MsduHandle;
	DATAREQ.TxOptions   = TxOptions;
	memcpy(DATAREQ.Msdu, pMsdu, MsduLength);
	pSec           = (struct SecSpec *)(DATAREQ.Msdu + MsduLength);
	Command.Length = sizeof(struct MCPS_DATA_request_pset) - MAX_DATA_SIZE + MsduLength;
	if ((pSecurity == NULL) || (pSecurity->SecurityLevel == 0))
	{
		pSec->SecurityLevel = 0;
		Command.Length += 1;
	}
	else
	{
		*pSec = *pSecurity;
		Command.Length += sizeof(struct SecSpec);
	}

	if (ca821x_api_downstream(&Command.CommandId, NULL, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	return MAC_SUCCESS;
#undef DATAREQ
} // End of MCPS_DATA_request()

ca_mac_status MCPS_PURGE_request_sync(uint8_t *MsduHandle, struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t CommandId;
		uint8_t Length;
		uint8_t MsduHandle;
	} Command;
	Command.CommandId  = SPI_MCPS_PURGE_REQUEST;
	Command.Length     = 1;
	Command.MsduHandle = *MsduHandle;

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_MCPS_PURGE_CONFIRM)
		return MAC_SYSTEM_ERROR;

	*MsduHandle = Response.PData.PurgeCnf.MsduHandle;

	return (ca_mac_status)Response.PData.PurgeCnf.Status;
} // End of MCPS_PURGE_request_sync()

#if CASCODA_CA_VER >= 8211
ca_mac_status PCPS_DATA_request(uint8_t            PsduHandle,
                                uint8_t            TxOpts,
                                uint8_t            PsduLength,
                                uint8_t *          pPsdu,
                                struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Command;

	if (PsduLength > aMaxPHYPacketSize)
		return MAC_FRAME_TOO_LONG;

#define DATAREQ (Command.PData.PhyDataReq)
	Command.CommandId  = SPI_PCPS_DATA_REQUEST;
	DATAREQ.PsduHandle = PsduHandle;
	DATAREQ.TxOpts     = TxOpts;
	DATAREQ.PsduLength = PsduLength;
	memcpy(DATAREQ.Psdu, pPsdu, PsduLength);
	Command.Length = PsduLength + sizeof(struct PCPS_DATA_request_pset) - aMaxPHYPacketSize;

	if (ca821x_api_downstream(&Command.CommandId, NULL, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	return MAC_SUCCESS;
#undef DATAREQ
} // End of PCPS_DATA_request()
#endif // CASCODA_CA_VER >= 8211

ca_mac_status MLME_ASSOCIATE_request(uint8_t            LogicalChannel,
                                     struct FullAddr    DstAddr,
                                     uint8_t            CapabilityInfo,
                                     struct SecSpec *   pSecurity,
                                     struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Command;
#define ASSOCREQ (Command.PData.AssocReq)

	Command.CommandId       = SPI_MLME_ASSOCIATE_REQUEST;
	Command.Length          = sizeof(struct MLME_ASSOCIATE_request_pset);
	ASSOCREQ.LogicalChannel = LogicalChannel;
	ASSOCREQ.Dst            = DstAddr;
	ASSOCREQ.CapabilityInfo = CapabilityInfo;
	if ((pSecurity == NULL) || (pSecurity->SecurityLevel == 0))
	{
		ASSOCREQ.Security.SecurityLevel = 0;
		Command.Length -= sizeof(struct SecSpec) - 1;
	}
	else
	{
		ASSOCREQ.Security = *pSecurity;
	}

	if (ca821x_api_downstream(&Command.CommandId, NULL, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	return MAC_SUCCESS;
#undef ASSOCREQ
} // End of MLME_ASSOCIATE_request()

ca_mac_status MLME_ASSOCIATE_response(uint8_t *          pDeviceAddress,
                                      uint16_t           AssocShortAddress,
                                      uint8_t            Status,
                                      struct SecSpec *   pSecurity,
                                      struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Command;
	Command.CommandId = SPI_MLME_ASSOCIATE_RESPONSE;
	Command.Length    = sizeof(struct MLME_ASSOCIATE_response_pset);
#define ASSOCRSP (Command.PData.AssocRsp)
	memcpy(ASSOCRSP.DeviceAddress, pDeviceAddress, 8);
	ASSOCRSP.AssocShortAddress[0] = LS_BYTE(AssocShortAddress);
	ASSOCRSP.AssocShortAddress[1] = MS_BYTE(AssocShortAddress);
	ASSOCRSP.Status               = Status;
	if ((pSecurity == NULL) || (pSecurity->SecurityLevel == 0))
	{
		ASSOCRSP.Security.SecurityLevel = 0;
		Command.Length -= sizeof(struct SecSpec) - 1;
	}
	else
	{
		ASSOCRSP.Security = *pSecurity;
	}

	if (ca821x_api_downstream(&Command.CommandId, NULL, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	return MAC_SUCCESS;
#undef ASSOCRSP
} // End of MLME_ASSOCIATE_response()

ca_mac_status MLME_DISASSOCIATE_request(struct FullAddr    DevAddr,
                                        uint8_t            DisassociateReason,
                                        uint8_t            TxIndirect,
                                        struct SecSpec *   pSecurity,
                                        struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Command;
	Command.CommandId = SPI_MLME_DISASSOCIATE_REQUEST;
	Command.Length    = sizeof(struct MLME_DISASSOCIATE_request_pset);

	Command.PData.DisassocReq.DevAddr            = DevAddr;
	Command.PData.DisassocReq.DisassociateReason = DisassociateReason;
	Command.PData.DisassocReq.TxIndirect         = TxIndirect;

	if ((pSecurity == NULL) || (pSecurity->SecurityLevel == 0))
	{
		Command.PData.DisassocReq.Security.SecurityLevel = 0;
		Command.Length -= sizeof(struct SecSpec) - 1;
	}
	else
	{
		Command.PData.DisassocReq.Security = *pSecurity;
	}

	if (ca821x_api_downstream(&Command.CommandId, NULL, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	return MAC_SUCCESS;
} // End of MLME_DISASSOCIATE_request()

ca_mac_status MLME_GET_request_sync(uint8_t            PIBAttribute,
                                    uint8_t            PIBAttributeIndex,
                                    uint8_t *          pPIBAttributeLength,
                                    void *             pPIBAttributeValue,
                                    struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;

	struct cmd
	{
		uint8_t                      CommandId;
		uint8_t                      Length;
		struct MLME_GET_request_pset GetReq;
	} Command;
#define GETREQ (Command.GetReq)
#define GETCNF (Response.PData.GetCnf)

	Command.CommandId        = SPI_MLME_GET_REQUEST;
	Command.Length           = sizeof(struct MLME_GET_request_pset);
	GETREQ.PIBAttribute      = PIBAttribute;
	GETREQ.PIBAttributeIndex = PIBAttributeIndex;

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_MLME_GET_CONFIRM)
		return MAC_SYSTEM_ERROR;

	if (GETCNF.Status == MAC_SUCCESS)
	{
		*pPIBAttributeLength = GETCNF.PIBAttributeLength;
		memcpy(pPIBAttributeValue, GETCNF.PIBAttributeValue, GETCNF.PIBAttributeLength);
	}

	return (ca_mac_status)GETCNF.Status;
#undef GETREQ
#undef GETCNF
} // End of MLME_GET_request_sync()

ca_mac_status MLME_ORPHAN_response(uint8_t *          pOrphanAddress,
                                   uint16_t           ShortAddress,
                                   uint8_t            AssociatedMember,
                                   struct SecSpec *   pSecurity,
                                   struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Command;
#define ORPHANRSP (Command.PData.OrphanRsp)
	Command.CommandId = SPI_MLME_ORPHAN_RESPONSE;
	Command.Length    = sizeof(struct MLME_ORPHAN_response_pset);
	memcpy(ORPHANRSP.OrphanAddress, pOrphanAddress, 8);
	ORPHANRSP.ShortAddress[0]  = LS_BYTE(ShortAddress);
	ORPHANRSP.ShortAddress[1]  = MS_BYTE(ShortAddress);
	ORPHANRSP.AssociatedMember = AssociatedMember;
	if ((pSecurity == NULL) || (pSecurity->SecurityLevel == 0))
	{
		ORPHANRSP.Security.SecurityLevel = 0;
		Command.Length -= sizeof(struct SecSpec) - 1;
	}
	else
	{
		ORPHANRSP.Security = *pSecurity;
	}

	if (ca821x_api_downstream(&Command.CommandId, NULL, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	return MAC_SUCCESS;
#undef ORPHANRSP
} // End of MLME_ORPHAN_response()

ca_mac_status MLME_RESET_request_sync(uint8_t SetDefaultPIB, struct ca821x_dev *pDeviceRef)
{
	uint8_t            status;
	struct MAC_Message Response;

	struct cmd
	{
		uint8_t CommandId;
		uint8_t Length;
		uint8_t SetDefaultPib;
	} Command;
#define SIMPLECNF (Response.PData)
	Command.CommandId     = SPI_MLME_RESET_REQUEST;
	Command.Length        = 1;
	Command.SetDefaultPib = SetDefaultPIB;

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_MLME_RESET_CONFIRM)
		return MAC_SYSTEM_ERROR;

	status = SIMPLECNF.Status;

#if (CASCODA_CA_VER == 8210)
	if (SetDefaultPIB && status == MAC_SUCCESS)
	{
		pDeviceRef->shortaddr = 0xFFFF;
	}
#endif

	return (ca_mac_status)status;
#undef SIMPLEREQ
#undef SIMPLECNF
} // End of MLME_RESET_request_sync()

ca_mac_status MLME_RX_ENABLE_request_sync(uint8_t            DeferPermit,
                                          uint32_t           RxOnTime,
                                          uint32_t           RxOnDuration,
                                          struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t                            CommandId;
		uint8_t                            Length;
		struct MLME_RX_ENABLE_request_pset RxEnableReq;
	} Command;

	Command.CommandId = SPI_MLME_RX_ENABLE_REQUEST;
	Command.Length    = sizeof(struct MLME_RX_ENABLE_request_pset);

	Command.RxEnableReq.DeferPermit     = DeferPermit;
	Command.RxEnableReq.RxOnTime[0]     = LS0_BYTE(RxOnTime);
	Command.RxEnableReq.RxOnTime[1]     = LS1_BYTE(RxOnTime);
	Command.RxEnableReq.RxOnTime[2]     = LS2_BYTE(RxOnTime);
	Command.RxEnableReq.RxOnTime[3]     = LS3_BYTE(RxOnTime);
	Command.RxEnableReq.RxOnDuration[0] = LS0_BYTE(RxOnDuration);
	Command.RxEnableReq.RxOnDuration[1] = LS1_BYTE(RxOnDuration);
	Command.RxEnableReq.RxOnDuration[2] = LS2_BYTE(RxOnDuration);
	Command.RxEnableReq.RxOnDuration[3] = LS3_BYTE(RxOnDuration);

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_MLME_RX_ENABLE_CONFIRM)
		return MAC_SYSTEM_ERROR;

	return (ca_mac_status)Response.PData.Status;
} // End of MLME_RX_ENABLE_request_sync()

ca_mac_status MLME_SCAN_request(uint8_t            ScanType,
                                uint32_t           ScanChannels,
                                uint8_t            ScanDuration,
                                struct SecSpec *   pSecurity,
                                struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Command;
#define SCANREQ (Command.PData.ScanReq)
	Command.CommandId       = SPI_MLME_SCAN_REQUEST;
	Command.Length          = sizeof(struct MLME_SCAN_request_pset);
	SCANREQ.ScanType        = ScanType;
	SCANREQ.ScanChannels[0] = LS0_BYTE(ScanChannels);
	SCANREQ.ScanChannels[1] = LS1_BYTE(ScanChannels);
	SCANREQ.ScanChannels[2] = LS2_BYTE(ScanChannels);
	SCANREQ.ScanChannels[3] = LS3_BYTE(ScanChannels);
	SCANREQ.ScanDuration    = ScanDuration;
	if ((pSecurity == NULL) || (pSecurity->SecurityLevel == 0))
	{
		SCANREQ.Security.SecurityLevel = 0;
		Command.Length -= sizeof(struct SecSpec) - 1;
	}
	else
	{
		SCANREQ.Security = *pSecurity;
	}

	if (ca821x_api_downstream(&Command.CommandId, NULL, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	return MAC_SUCCESS;
#undef SCANREQ
} // End of MLME_SCAN_request()

ca_mac_status MLME_SET_request_sync(uint8_t            PIBAttribute,
                                    uint8_t            PIBAttributeIndex,
                                    uint8_t            PIBAttributeLength,
                                    const void *       pPIBAttributeValue,
                                    struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t                      CommandId;
		uint8_t                      Length;
		struct MLME_SET_request_pset SetReq;
	} Command;

#define SETREQ (Command.SetReq)
#define SIMPLECNF (Response.PData)

	Command.CommandId         = SPI_MLME_SET_REQUEST;
	Command.Length            = sizeof(struct MLME_SET_request_pset) - MAX_ATTRIBUTE_SIZE + PIBAttributeLength;
	SETREQ.PIBAttribute       = PIBAttribute;
	SETREQ.PIBAttributeIndex  = PIBAttributeIndex;
	SETREQ.PIBAttributeLength = PIBAttributeLength;
	memcpy(SETREQ.PIBAttributeValue, pPIBAttributeValue, PIBAttributeLength);

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_MLME_SET_CONFIRM)
		return MAC_SYSTEM_ERROR;

#if (CASCODA_CA_VER == 8210)
	if (SIMPLECNF.Status == MAC_SUCCESS)
	{
		if (PIBAttribute == macShortAddress)
		{
			pDeviceRef->shortaddr = GETLE16(pPIBAttributeValue);
		}
		else if (PIBAttribute == nsIEEEAddress)
		{
			memcpy(pDeviceRef->extaddr, pPIBAttributeValue, 8);
		}
	}
#endif

	return (ca_mac_status)SIMPLECNF.Status;
#undef SETREQ
#undef SIMPLECNF
} // End of MLME_SET_request_sync()

ca_mac_status MLME_START_request_sync(uint16_t           PANId,
                                      uint8_t            LogicalChannel,
                                      uint8_t            BeaconOrder,
                                      uint8_t            SuperframeOrder,
                                      uint8_t            PANCoordinator,
                                      uint8_t            BatteryLifeExtension,
                                      uint8_t            CoordRealignment,
                                      struct SecSpec *   pCoordRealignSecurity,
                                      struct SecSpec *   pBeaconSecurity,
                                      struct ca821x_dev *pDeviceRef)
{
	struct SecSpec *   pBS;
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t                        CommandId;
		uint8_t                        Length;
		struct MLME_START_request_pset StartReq;
	} Command;
#define STARTREQ (Command.StartReq)

	Command.CommandId             = SPI_MLME_START_REQUEST;
	Command.Length                = sizeof(struct MLME_START_request_pset);
	STARTREQ.PANId[0]             = LS_BYTE(PANId);
	STARTREQ.PANId[1]             = MS_BYTE(PANId);
	STARTREQ.LogicalChannel       = LogicalChannel;
	STARTREQ.BeaconOrder          = BeaconOrder;
	STARTREQ.SuperframeOrder      = SuperframeOrder;
	STARTREQ.PANCoordinator       = PANCoordinator;
	STARTREQ.BatteryLifeExtension = BatteryLifeExtension;
	STARTREQ.CoordRealignment     = CoordRealignment;
	if ((pCoordRealignSecurity == NULL) || (pCoordRealignSecurity->SecurityLevel == 0))
	{
		STARTREQ.CoordRealignSecurity.SecurityLevel = 0;
		Command.Length -= sizeof(struct SecSpec) - 1;
		pBS = (struct SecSpec *)(&STARTREQ.CoordRealignSecurity.SecurityLevel + 1);
	}
	else
	{
		STARTREQ.CoordRealignSecurity = *pCoordRealignSecurity;
		pBS                           = &STARTREQ.BeaconSecurity;
	}
	if ((pBeaconSecurity == NULL) || (pBeaconSecurity->SecurityLevel == 0))
	{
		pBS->SecurityLevel = 0;
		Command.Length -= sizeof(struct SecSpec) - 1;
	}
	else
	{
		*pBS = *pBeaconSecurity;
	}

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_MLME_START_CONFIRM)
		return MAC_SYSTEM_ERROR;

	return (ca_mac_status)Response.PData.Status;
#undef STARTREQ
} // End of MLME_START_request_sync()

ca_mac_status MLME_POLL_request_sync(struct FullAddr CoordAddress,
#if CASCODA_CA_VER == 8210
                                     uint8_t Interval[2], /* polling interval in 0.1 seconds res */
                                                          /* 0 means poll once */
                                                          /* 0xFFFF means stop polling */
#endif
                                     struct SecSpec *   pSecurity,
                                     struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t                       CommandId;
		uint8_t                       Length;
		struct MLME_POLL_request_pset PollReq;
	} Command;
#define POLLREQ (Command.PollReq)
	Command.CommandId    = SPI_MLME_POLL_REQUEST;
	Command.Length       = sizeof(struct MLME_POLL_request_pset);
	POLLREQ.CoordAddress = CoordAddress;
#if CASCODA_CA_VER == 8210
	POLLREQ.Interval[0] = Interval[0];
	POLLREQ.Interval[1] = Interval[1];
#endif
	if ((pSecurity == NULL) || (pSecurity->SecurityLevel == 0))
	{
		POLLREQ.Security.SecurityLevel = 0;
		Command.Length -= sizeof(struct SecSpec) - 1;
	}
	else
	{
		POLLREQ.Security = *pSecurity;
	}

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_MLME_POLL_CONFIRM)
		return MAC_SYSTEM_ERROR;

	return (ca_mac_status)Response.PData.Status;
#undef POLLREQ
} // End of MLME_POLL_request_sync()

ca_mac_status HWME_SET_request_sync(uint8_t            HWAttribute,
                                    uint8_t            HWAttributeLength,
                                    uint8_t *          pHWAttributeValue,
                                    struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t                      CommandId;
		uint8_t                      Length;
		struct HWME_SET_request_pset HWMESetReq;
	} Command;
	Command.CommandId                    = SPI_HWME_SET_REQUEST;
	Command.Length                       = 2 + HWAttributeLength;
	Command.HWMESetReq.HWAttribute       = HWAttribute;
	Command.HWMESetReq.HWAttributeLength = HWAttributeLength;
	memcpy(Command.HWMESetReq.HWAttributeValue, pHWAttributeValue, HWAttributeLength);

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_HWME_SET_CONFIRM)
		return MAC_SYSTEM_ERROR;

#if (CASCODA_CA_VER == 8210)
	if (HWAttribute == HWME_LQIMODE && Response.PData.Status == MAC_SUCCESS)
		pDeviceRef->lqi_mode = *pHWAttributeValue;
#endif

	return (ca_mac_status)Response.PData.HWMESetCnf.Status;
} // End of HWME_SET_request_sync()

ca_mac_status HWME_GET_request_sync(uint8_t            HWAttribute,
                                    uint8_t *          HWAttributeLength,
                                    uint8_t *          pHWAttributeValue,
                                    struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t                      CommandId;
		uint8_t                      Length;
		struct HWME_GET_request_pset HWMEGetReq;
	} Command;
	Command.CommandId              = SPI_HWME_GET_REQUEST;
	Command.Length                 = 1;
	Command.HWMEGetReq.HWAttribute = HWAttribute;

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_HWME_GET_CONFIRM)
		return MAC_SYSTEM_ERROR;

	if (Response.PData.HWMEGetCnf.Status == MAC_SUCCESS)
	{
		*HWAttributeLength = Response.PData.HWMEGetCnf.HWAttributeLength;
		memcpy(pHWAttributeValue, Response.PData.HWMEGetCnf.HWAttributeValue, *HWAttributeLength);
	}

	return (ca_mac_status)Response.PData.HWMEGetCnf.Status;
} // End of HWME_GET_request_sync()

ca_mac_status HWME_HAES_request_sync(uint8_t HAESMode, uint8_t *pHAESData, struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t                       CommandId;
		uint8_t                       Length;
		struct HWME_HAES_request_pset HWMEHAESReq;
	} Command;
	Command.CommandId            = SPI_HWME_HAES_REQUEST;
	Command.Length               = 17;
	Command.HWMEHAESReq.HAESMode = HAESMode;
	memcpy(Command.HWMEHAESReq.HAESData, pHAESData, 16);

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_HWME_HAES_CONFIRM)
		return MAC_SYSTEM_ERROR;

	if (Response.PData.HWMEHAESCnf.Status == MAC_SUCCESS)
		memcpy(pHAESData, Response.PData.HWMEHAESCnf.HAESData, 16);

	return (ca_mac_status)Response.PData.HWMEHAESCnf.Status;
} // End of HWME_HAES_request_sync()

ca_mac_status TDME_SETSFR_request_sync(uint8_t            SFRPage,
                                       uint8_t            SFRAddress,
                                       uint8_t            SFRValue,
                                       struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t                         CommandId;
		uint8_t                         Length;
		struct TDME_SETSFR_request_pset TDMESetSFRReq;
	} Command;
	Command.CommandId                = SPI_TDME_SETSFR_REQUEST;
	Command.Length                   = 3;
	Command.TDMESetSFRReq.SFRPage    = SFRPage;
	Command.TDMESetSFRReq.SFRAddress = SFRAddress;
	Command.TDMESetSFRReq.SFRValue   = SFRValue;
	Response.CommandId               = 0xFF;
	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_TDME_SETSFR_CONFIRM)
		return MAC_SYSTEM_ERROR;

	return (ca_mac_status)Response.PData.TDMESetSFRCnf.Status;
} // End of TDME_SETSFR_request_sync()

ca_mac_status TDME_GETSFR_request_sync(uint8_t            SFRPage,
                                       uint8_t            SFRAddress,
                                       uint8_t *          SFRValue,
                                       struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t                         CommandId;
		uint8_t                         Length;
		struct TDME_GETSFR_request_pset TDMEGetSFRReq;
	} Command;
	Command.CommandId                = SPI_TDME_GETSFR_REQUEST;
	Command.Length                   = 2;
	Command.TDMEGetSFRReq.SFRPage    = SFRPage;
	Command.TDMEGetSFRReq.SFRAddress = SFRAddress;

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_TDME_GETSFR_CONFIRM)
		return MAC_SYSTEM_ERROR;

	*SFRValue = Response.PData.TDMEGetSFRCnf.SFRValue;

	return (ca_mac_status)Response.PData.TDMEGetSFRCnf.Status;
} // End of TDME_GETSFR_request_sync()

ca_mac_status TDME_TESTMODE_request_sync(uint8_t TestMode, struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t                           CommandId;
		uint8_t                           Length;
		struct TDME_TESTMODE_request_pset TDMETestModeReq;
	} Command;
	Command.CommandId                = SPI_TDME_TESTMODE_REQUEST;
	Command.Length                   = 1;
	Command.TDMETestModeReq.TestMode = TestMode;

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_TDME_TESTMODE_CONFIRM)
		return MAC_SYSTEM_ERROR;

	return (ca_mac_status)Response.PData.TDMETestModeCnf.Status;
} // End of TDME_TESTMODE_request_sync()

ca_mac_status TDME_SET_request_sync(uint8_t            TestAttribute,
                                    uint8_t            TestAttributeLength,
                                    void *             pTestAttributeValue,
                                    struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t                      CommandId;
		uint8_t                      Length;
		struct TDME_SET_request_pset TDMESetReq;
	} Command;

	Command.CommandId                    = SPI_TDME_SET_REQUEST;
	Command.Length                       = 2 + TestAttributeLength;
	Command.TDMESetReq.TDAttribute       = TestAttribute;
	Command.TDMESetReq.TDAttributeLength = TestAttributeLength;
	memcpy(Command.TDMESetReq.TDAttributeValue, pTestAttributeValue, TestAttributeLength);

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_TDME_SET_CONFIRM)
		return MAC_SYSTEM_ERROR;

	return (ca_mac_status)Response.PData.TDMESetCnf.Status;
} // End of TDME_SET_request_sync()

ca_mac_status TDME_TXPKT_request_sync(uint8_t            TestPacketDataType,
                                      uint8_t *          TestPacketSequenceNumber,
                                      uint8_t *          TestPacketLength,
                                      void *             pTestPacketData,
                                      struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t                        CommandId;
		uint8_t                        Length;
		struct TDME_TXPKT_request_pset TDMETxPktReq;
	} Command;

	Command.CommandId = SPI_TDME_TXPKT_REQUEST;
	if (TestPacketDataType == TDME_TXD_APPENDED)
	{
		Command.Length = 3 + *TestPacketLength;
	}
	else
	{
		Command.Length = 3;
	}

	Command.TDMETxPktReq.TestPacketDataType       = TestPacketDataType;
	Command.TDMETxPktReq.TestPacketSequenceNumber = *TestPacketSequenceNumber;
	Command.TDMETxPktReq.TestPacketLength         = *TestPacketLength;

	if (TestPacketDataType == TDME_TXD_APPENDED)
		memcpy(Command.TDMETxPktReq.TestPacketData, pTestPacketData, *TestPacketLength);

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_TDME_TXPKT_CONFIRM)
		return MAC_SYSTEM_ERROR;

	if (Response.PData.TDMETxPktCnf.Status == TDME_SUCCESS)
	{
		*TestPacketLength         = Response.PData.TDMETxPktCnf.TestPacketLength;
		*TestPacketSequenceNumber = Response.PData.TDMETxPktCnf.TestPacketSequenceNumber;
		memcpy(pTestPacketData, Response.PData.TDMETxPktCnf.TestPacketData, *TestPacketLength);
	}

	return (ca_mac_status)Response.PData.TDMETxPktCnf.Status;
} // End of TDME_TXPKT_request_sync()

ca_mac_status TDME_LOTLK_request_sync(uint8_t *          TestChannel,
                                      uint8_t *          TestRxTxb,
                                      uint8_t *          TestLOFDACValue,
                                      uint8_t *          TestLOAMPValue,
                                      uint8_t *          TestLOTXCALValue,
                                      struct ca821x_dev *pDeviceRef)
{
	struct MAC_Message Response;
	struct cmd
	{
		uint8_t                        CommandId;
		uint8_t                        Length;
		struct TDME_LOTLK_request_pset TDMELOTlkReq;
	} Command;

	Command.CommandId                = SPI_TDME_LOTLK_REQUEST;
	Command.Length                   = 2;
	Command.TDMELOTlkReq.TestChannel = *TestChannel;
	Command.TDMELOTlkReq.TestRxTxb   = *TestRxTxb;

	if (ca821x_api_downstream(&Command.CommandId, &Response.CommandId, pDeviceRef))
		return MAC_SYSTEM_ERROR;

	if (Response.CommandId != SPI_TDME_LOTLK_CONFIRM)
		return MAC_SYSTEM_ERROR;

	if (Response.PData.TDMELOTlkCnf.Status == TDME_SUCCESS)
	{
		*TestChannel      = Response.PData.TDMELOTlkCnf.TestChannel;
		*TestRxTxb        = Response.PData.TDMELOTlkCnf.TestRxTxb;
		*TestLOFDACValue  = Response.PData.TDMELOTlkCnf.TestLOFDACValue;
		*TestLOAMPValue   = Response.PData.TDMELOTlkCnf.TestLOAMPValue;
		*TestLOTXCALValue = Response.PData.TDMELOTlkCnf.TestLOTXCALValue;
	}

	return (ca_mac_status)Response.PData.TDMELOTlkCnf.Status;
} // End of TDME_LOTLK_request_sync()

ca_mac_status TDME_ChipInit(struct ca821x_dev *pDeviceRef)
{
	uint8_t status;

	if ((status = TDME_SETSFR_request_sync(1, 0xE1, 0x29, pDeviceRef))) // LNA Gain Settings
		return (ca_mac_status)(status);
	if ((status = TDME_SETSFR_request_sync(1, 0xE2, 0x54, pDeviceRef)))
		return (ca_mac_status)(status);
	if ((status = TDME_SETSFR_request_sync(1, 0xE3, 0x6C, pDeviceRef)))
		return (ca_mac_status)(status);
	if ((status = TDME_SETSFR_request_sync(1, 0xE4, 0x7A, pDeviceRef)))
		return (ca_mac_status)(status);
	if ((status = TDME_SETSFR_request_sync(1, 0xE5, 0x84, pDeviceRef)))
		return (ca_mac_status)(status);
	if ((status = TDME_SETSFR_request_sync(1, 0xE6, 0x8B, pDeviceRef)))
		return (ca_mac_status)(status);
	if ((status = TDME_SETSFR_request_sync(1, 0xE7, 0x92, pDeviceRef)))
		return (ca_mac_status)(status);
	if ((status = TDME_SETSFR_request_sync(1, 0xE9, 0x96, pDeviceRef)))
		return (ca_mac_status)(status);
	if ((status = TDME_SETSFR_request_sync(1, 0xD3, 0x5B, pDeviceRef))) // Preamble Timing Config
		return (ca_mac_status)(status);
	if ((status = TDME_SETSFR_request_sync(1, 0xD1, 0x5A, pDeviceRef))) // Preamble Threshold High
		return (ca_mac_status)(status);
	if ((status = TDME_SETSFR_request_sync(0, 0xFE, 0x3F, pDeviceRef))) // Tx Output Power 8 dBm
		return (ca_mac_status)(status);

#if CASCODA_CA_VER == 8210
	/* Set hardware lqi limit to 0 to disable lqi-based frame filtering */
	uint8_t lqi_limit = 0;
	if ((status = HWME_SET_request_sync(0x11, 1, &lqi_limit, pDeviceRef)))
		return (ca_mac_status)(status);
#endif

	return MAC_SUCCESS;
} // End of TDME_ChipInit()

ca_mac_status TDME_ChannelInit(uint8_t channel, struct ca821x_dev *pDeviceRef)
{
	uint8_t txcalval;

	if (channel >= 25)
	{
		txcalval = 0xA7;
	}
	else if (channel >= 23)
	{
		txcalval = 0xA8;
	}
	else if (channel >= 22)
	{
		txcalval = 0xA9;
	}
	else if (channel >= 20)
	{
		txcalval = 0xAA;
	}
	else if (channel >= 17)
	{
		txcalval = 0xAB;
	}
	else if (channel >= 16)
	{
		txcalval = 0xAC;
	}
	else if (channel >= 14)
	{
		txcalval = 0xAD;
	}
	else if (channel >= 12)
	{
		txcalval = 0xAE;
	}
	else
	{
		txcalval = 0xAF;
	}

	return TDME_SETSFR_request_sync(1, 0xBF, txcalval, pDeviceRef); // LO Tx Cal
} // End of TDME_ChannelInit()

ca_mac_status TDME_CheckPIBAttribute(uint8_t PIBAttribute, uint8_t PIBAttributeLength, const void *pPIBAttributeValue)
{
	uint8_t status = MAC_SUCCESS;
	uint8_t value;

	value = *((uint8_t *)pPIBAttributeValue);

	switch (PIBAttribute)
	{
	/* PHY */
	case phyCurrentChannel:
		if (value < 11 || value > 26)
			status = MAC_INVALID_PARAMETER;
		break;
	case phyTransmitPower:
		if (value > 0x3F)
			status = MAC_INVALID_PARAMETER;
		break;
	case phyCCAMode:
		if (value > 0x03)
			status = MAC_INVALID_PARAMETER;
		break;
	/* MAC */
	case macBattLifeExtPeriods:
		if ((value < 6) || (value > 41))
			status = MAC_INVALID_PARAMETER;
		break;
	case macBeaconPayload:
		if (PIBAttributeLength > aMaxBeaconPayloadLength)
			status = MAC_INVALID_PARAMETER;
		break;
	case macBeaconPayloadLength:
		if (value > aMaxBeaconPayloadLength)
			status = MAC_INVALID_PARAMETER;
		break;
	case macBeaconOrder:
		if (value > 15)
			status = MAC_INVALID_PARAMETER;
		break;
	case macMaxBE:
		if ((value < 3) || (value > 8))
			status = MAC_INVALID_PARAMETER;
		break;
	case macMaxCSMABackoffs:
		if (value > 5)
			status = MAC_INVALID_PARAMETER;
		break;
	case macMaxFrameRetries:
		if (value > 7)
			status = MAC_INVALID_PARAMETER;
		break;
	case macMinBE:
		if (value > 8)
			status = MAC_INVALID_PARAMETER;
		break;
	case macResponseWaitTime:
		if ((value < 2) || (value > 64))
			status = MAC_INVALID_PARAMETER;
		break;
	case macSuperframeOrder:
		if (value > 15)
			status = MAC_INVALID_PARAMETER;
		break;
	/* boolean */
	case macAssociatedPANCoord:
	case macAssociationPermit:
	case macAutoRequest:
	case macBattLifeExt:
	case macGTSPermit:
	case macPromiscuousMode:
	case macRxOnWhenIdle:
	case macSecurityEnabled:
		if (value > 1)
			status = MAC_INVALID_PARAMETER;
		break;
	/* MAC SEC */
	case macAutoRequestSecurityLevel:
		if (value > 7)
			status = MAC_INVALID_PARAMETER;
		break;
	case macAutoRequestKeyIdMode:
		if (value > 3)
			status = MAC_INVALID_PARAMETER;
		break;
	default:
		break;
	}

	return (ca_mac_status)status;
}

ca_mac_status TDME_SetTxPower(uint8_t txp, struct ca821x_dev *pDeviceRef)
{
	uint8_t status;
	int8_t  txp_val;
	uint8_t txp_ext;
	uint8_t paib;

	/* extend from 6 to 8 bit */
	txp_ext = 0x3F & txp;
	if (txp_ext & 0x20)
		txp_ext += 0xC0;
	txp_val = (int8_t)txp_ext;

	if (pDeviceRef->MAC_MPW)
	{
		if (txp_val > 0)
		{
			paib = 0xD3; /* 8 dBm: ptrim = 5, itrim = +3 => +4 dBm */
		}
		else
		{
			paib = 0x73; /* 0 dBm: ptrim = 7, itrim = +3 => -6 dBm */
		}
		/* write PACFG */
		status = TDME_SETSFR_request_sync(0, 0xB1, paib, pDeviceRef);
	}
	else
	{
		/* Look-Up Table for Setting Current and Frequency Trim values for desired Output Power */
		if (txp_val > 8)
		{
			paib = 0x3F;
		}
		else if (txp_val == 8)
		{
			paib = 0x32;
		}
		else if (txp_val == 7)
		{
			paib = 0x22;
		}
		else if (txp_val == 6)
		{
			paib = 0x18;
		}
		else if (txp_val == 5)
		{
			paib = 0x10;
		}
		else if (txp_val == 4)
		{
			paib = 0x0C;
		}
		else if (txp_val == 3)
		{
			paib = 0x08;
		}
		else if (txp_val == 2)
		{
			paib = 0x05;
		}
		else if (txp_val == 1)
		{
			paib = 0x03;
		}
		else if (txp_val == 0)
		{
			paib = 0x01;
		}
		else /*  <  0 */
		{
			paib = 0x00;
		}
		/* write PACFGIB */
		status = TDME_SETSFR_request_sync(0, 0xFE, paib, pDeviceRef);
	}

	return (ca_mac_status)status;
}

ca_mac_status TDME_GetTxPower(uint8_t *txp, struct ca821x_dev *pDeviceRef)
{
	uint8_t status;
	uint8_t paib = 0;
	int8_t  txp_val;

	if (pDeviceRef->MAC_MPW)
	{
		status = TDME_GETSFR_request_sync(0, 0xB1, &paib, pDeviceRef); /* read PACFG */

		if (paib & 0x80)
		{                /* BOOST? */
			txp_val = 4; /* +4 dBm */
		}
		else
		{
			txp_val = -6; /* -6 dBm */
		}

		/* limit to 6 bit */
		*txp = (uint8_t)(txp_val)&0x3F;
		*txp += (0x01 << 6); /* tolerance +-3 dB */
	}
	else
	{
		status = TDME_GETSFR_request_sync(0, 0xFE, &paib, pDeviceRef); // read PACFGIB

		if (paib >= 0x32)
			txp_val = 8;
		else if (paib >= 0x22)
			txp_val = 7;
		else if (paib >= 0x18)
			txp_val = 6;
		else if (paib >= 0x10)
			txp_val = 5;
		else if (paib >= 0x0C)
			txp_val = 4;
		else if (paib >= 0x08)
			txp_val = 3;
		else if (paib >= 0x05)
			txp_val = 2;
		else if (paib >= 0x03)
			txp_val = 1;
		else if (paib > 0x00)
			txp_val = 0;
		else
			txp_val = -1;

		/* limit to 6 bit */
		*txp = (uint8_t)(txp_val)&0x3F;

		//                      /* tolerance +-1 dB */
		// txp += (0x01 << 6);  /* tolerance +-3 dB */
		// txp += (0x02 << 6);  /* tolerance +-6 dB */
	}

	return (ca_mac_status)status;
}

#if (CASCODA_CA_VER == 8210)
/******************************************************************************/
/***************************************************************************/ /**
 * \brief Checks a data indication to ensure that its destination address
 *        matches the address of this node.
 *******************************************************************************
 * \param *ind - Data indication parameter set
 *******************************************************************************
 * \return Result of address check
 *         0: Success
 *         -1: Address mismatch (non-unix)
 *         -EFAULT: Address mismatch (unix)
 *******************************************************************************
 ******************************************************************************/
static ca_error check_data_ind_destaddr(struct MCPS_DATA_indication_pset *ind, struct ca821x_dev *pDeviceRef)
{
	int i;
	if (ind->Dst.AddressMode == MAC_MODE_SHORT_ADDR)
	{
		if (GETLE16(ind->Dst.Address) != MAC_BROADCAST_ADDRESS &&
		    memcmp(ind->Dst.Address, &(pDeviceRef->shortaddr), 2) != 0 && pDeviceRef->shortaddr != 0xFFFF)
		{
			return (CA_ERROR_FAIL);
		}
	}
	else if (ind->Dst.AddressMode == MAC_MODE_LONG_ADDR)
	{
		if (memcmp(ind->Dst.Address, pDeviceRef->extaddr, 8) != 0)
		{
			for (i = 0; i < 9; i++)
			{
				if (i == 8)
					return (CA_ERROR_SUCCESS);
				else if (pDeviceRef->extaddr[i] != 0)
					break;
			}
			return (CA_ERROR_FAIL);
		}
	}
	return (CA_ERROR_SUCCESS);
}
#endif //(CASCODA_CA_VER == 8210)

#if CASCODA_CA_VER == 8210
/******************************************************************************/
/***************************************************************************/ /**
 * \brief Checks an associate confirm for an assigned short address
 *******************************************************************************
 * \param *assoc_cnf - Associate confirm parameter set
 *******************************************************************************
 ******************************************************************************/
static void get_assoccnf_shortaddr(struct MLME_ASSOCIATE_confirm_pset *assoc_cnf, struct ca821x_dev *pDeviceRef)
{
	if (GETLE16(assoc_cnf->AssocShortAddress) != 0xFFFF)
	{
		pDeviceRef->shortaddr = GETLE16(assoc_cnf->AssocShortAddress);
	}
}
#endif

#if CASCODA_CA_VER == 8210
/******************************************************************************/
/***************************************************************************/ /**
 * \brief Checks a scan confirm for pan descriptor entries that have a beacon
 *        LQI below API_LQI_LIMIT and remove them.
 *******************************************************************************
 * \param *scan_cnf - Scan confirm message buffer
 *******************************************************************************
 ******************************************************************************/
static void verify_scancnf_results(struct MAC_Message *scan_cnf, struct ca821x_dev *pDeviceRef)
{
	struct MLME_SCAN_confirm_pset *scan_cnf_pset;
	struct PanDescriptor *         pdesc;
	int                            pdesc_index, pdesc_length;
	bool                           list_modified = false;

	scan_cnf_pset = &scan_cnf->PData.ScanCnf;
	if (pDeviceRef->lqi_mode == HWME_LQIMODE_ED) /* Cannot filter by ED */
		return;
	if (scan_cnf_pset->ScanType != ACTIVE_SCAN && scan_cnf_pset->ScanType != PASSIVE_SCAN)
		return;

	pdesc       = (struct PanDescriptor *)scan_cnf_pset->ResultList;
	pdesc_index = 0;
	while (pdesc_index < scan_cnf_pset->ResultListSize)
	{
		pdesc_length = 22;
		if (pdesc->Security.SecurityLevel > 0)
		{
			pdesc_length += 10;
		}
		if (pdesc->LinkQuality > API_LQI_LIMIT)
		{
			/* LQI is acceptable, move to next entry */
			pdesc = (struct PanDescriptor *)((uint8_t *)pdesc + pdesc_length);
			pdesc_index++;
			continue;
		}
		list_modified = true;
		/* Copy rest of list forward one index */
		memcpy(pdesc, (uint8_t *)pdesc + pdesc_length, scan_cnf->Length - (7 + pdesc_length));
		/* Update ResultListSize and command length */
		scan_cnf_pset->ResultListSize--;
		scan_cnf->Length -= pdesc_length;
	}
	if (scan_cnf_pset->ResultListSize == 0 && list_modified &&
	    (scan_cnf_pset->Status == MAC_SUCCESS || scan_cnf_pset->Status == MAC_LIMIT_REACHED))
	{
		scan_cnf_pset->Status = MAC_NO_BEACON;
	}
}
#endif

union ca821x_api_callback *ca821x_get_callback(uint8_t cmdid, struct ca821x_dev *pDeviceRef)
{
	union ca821x_api_callback *  rval      = NULL;
	struct ca821x_api_callbacks *callbacks = &(pDeviceRef->callbacks);

	switch (cmdid)
	{
	case SPI_MCPS_DATA_INDICATION:
		rval = (union ca821x_api_callback *)&callbacks->MCPS_DATA_indication;
		break;
	case SPI_MCPS_DATA_CONFIRM:
		rval = (union ca821x_api_callback *)&callbacks->MCPS_DATA_confirm;
		break;
#if CASCODA_CA_VER >= 8211
	case SPI_PCPS_DATA_INDICATION:
		rval = (union ca821x_api_callback *)&callbacks->PCPS_DATA_indication;
		break;
	case SPI_PCPS_DATA_CONFIRM:
		rval = (union ca821x_api_callback *)&callbacks->PCPS_DATA_confirm;
		break;
#endif //CASCODA_CA_VER >= 8211
	case SPI_MLME_ASSOCIATE_INDICATION:
		rval = (union ca821x_api_callback *)&callbacks->MLME_ASSOCIATE_indication;
		break;
	case SPI_MLME_ASSOCIATE_CONFIRM:
		rval = (union ca821x_api_callback *)&callbacks->MLME_ASSOCIATE_confirm;
		break;
	case SPI_MLME_DISASSOCIATE_INDICATION:
		rval = (union ca821x_api_callback *)&callbacks->MLME_DISASSOCIATE_indication;
		break;
	case SPI_MLME_DISASSOCIATE_CONFIRM:
		rval = (union ca821x_api_callback *)&callbacks->MLME_DISASSOCIATE_confirm;
		break;
	case SPI_MLME_BEACON_NOTIFY_INDICATION:
		rval = (union ca821x_api_callback *)&callbacks->MLME_BEACON_NOTIFY_indication;
		break;
	case SPI_MLME_ORPHAN_INDICATION:
		rval = (union ca821x_api_callback *)&callbacks->MLME_ORPHAN_indication;
		break;
	case SPI_MLME_SCAN_CONFIRM:
		rval = (union ca821x_api_callback *)&callbacks->MLME_SCAN_confirm;
		break;
	case SPI_MLME_COMM_STATUS_INDICATION:
		rval = (union ca821x_api_callback *)&callbacks->MLME_COMM_STATUS_indication;
		break;
	case SPI_MLME_SYNC_LOSS_INDICATION:
		rval = (union ca821x_api_callback *)&callbacks->MLME_SYNC_LOSS_indication;
		break;
#if CASCODA_CA_VER >= 8211
	case SPI_MLME_POLL_INDICATION:
		rval = (union ca821x_api_callback *)&callbacks->MLME_POLL_indication;
		break;
#endif //CASCODA_CA_VER >= 8211
	case SPI_HWME_WAKEUP_INDICATION:
		rval = (union ca821x_api_callback *)&callbacks->HWME_WAKEUP_indication;
		break;
	case SPI_TDME_RXPKT_INDICATION:
		rval = (union ca821x_api_callback *)&callbacks->TDME_RXPKT_indication;
		break;
	case SPI_TDME_EDDET_INDICATION:
		rval = (union ca821x_api_callback *)&callbacks->TDME_EDDET_indication;
		break;
	case SPI_TDME_ERROR_INDICATION:
		rval = (union ca821x_api_callback *)&callbacks->TDME_ERROR_indication;
		break;
	}

	return rval;
}

#if CASCODA_MAC_BLACKLIST != 0

static uint8_t blacklist_must_filter(struct MAC_Message *msg, struct ca821x_dev *pDeviceRef)
{
	struct FullAddr src;
	size_t          address_len;
	src.AddressMode = MAC_MODE_NO_ADDR;

	switch (msg->CommandId)
	{
	case SPI_MCPS_DATA_INDICATION:
		src = msg->PData.DataInd.Src;
		break;
	case SPI_MLME_ASSOCIATE_INDICATION:
		src.AddressMode = MAC_MODE_LONG_ADDR;
		memcpy(src.Address, msg->PData.AssocInd.DeviceAddress, sizeof(src.Address));
		break;
	case SPI_MLME_BEACON_NOTIFY_INDICATION:
		src = msg->PData.BeaconInd.PanDescriptor.Coord;
		break;
	case SPI_MLME_COMM_STATUS_INDICATION:
		src.AddressMode = msg->PData.CommStatusInd.SrcAddrMode;
		memcpy(src.Address, msg->PData.CommStatusInd.SrcAddr, sizeof(src.Address));
		break;
	case SPI_MLME_DISASSOCIATE_INDICATION:
		src.AddressMode = MAC_MODE_LONG_ADDR;
		memcpy(src.Address, msg->PData.DisassocInd.DevAddr, sizeof(src.Address));
		break;
	case SPI_MLME_ORPHAN_INDICATION:
		src.AddressMode = MAC_MODE_LONG_ADDR;
		memcpy(src.Address, msg->PData.OrphanInd.OrphanAddr, sizeof(src.Address));
		break;
#if CASCODA_CA_VER >= 8211
	case SPI_MLME_POLL_INDICATION:
		src = msg->PData.PollInd.Src;
		break;
#endif
	}

	if (src.AddressMode == MAC_MODE_SHORT_ADDR)
		address_len = 2;
	else if (src.AddressMode == MAC_MODE_LONG_ADDR)
		address_len = 8;
	else
		address_len = 0;

	if (address_len)
	{
		for (uint8_t i = 0; i < CASCODA_MAC_BLACKLIST; ++i)
		{
			if ((pDeviceRef->blacklist[i].AddressMode == src.AddressMode &&
			     (memcmp(src.Address, pDeviceRef->blacklist[i].Address, address_len) == 0)))
			{
				return 1;
			}
		}
	}
	return 0;
}
#endif

ca_error ca821x_downstream_dispatch(struct MAC_Message *msg, struct ca821x_dev *pDeviceRef)
{
	ca_error ret = CA_ERROR_NOT_HANDLED;

	union ca821x_api_callback *rval = ca821x_get_callback(msg->CommandId, pDeviceRef);

	if (!rval) //Unrecognised command ID
	{
		ret = CA_ERROR_INVALID_ARGS;
		goto exit;
	}

	/* call appropriate checks/updates/workarounds */
#if CASCODA_CA_VER == 8210
	switch (msg->CommandId)
	{
	case SPI_MCPS_DATA_INDICATION:
		ret = check_data_ind_destaddr(&msg->PData.DataInd, pDeviceRef);
		if (ret)
		{
			goto exit;
		}
		break;
	case SPI_MLME_SCAN_CONFIRM:
		verify_scancnf_results(msg, pDeviceRef);
		break;
	case SPI_MLME_ASSOCIATE_CONFIRM:
		get_assoccnf_shortaddr(&msg->PData.AssocCnf, pDeviceRef);
		break;
	}
#endif

#if CASCODA_MAC_BLACKLIST != 0
	if (blacklist_must_filter(msg, pDeviceRef))
	{
		ret = CA_ERROR_SUCCESS;
		goto exit;
	}
#endif // CASCODA_MAC_BLACKLIST

	//If there is a callback registered, call it. Otherwise (or if not handled), call the generic dispatch.
	ret = CA_ERROR_NOT_HANDLED;
	if (rval->generic_callback)
	{
		ret = rval->generic_callback(msg->PData.Payload, pDeviceRef);
	}
	if (ret == CA_ERROR_NOT_HANDLED && pDeviceRef->callbacks.generic_dispatch)
	{
		ret = pDeviceRef->callbacks.generic_dispatch(msg, pDeviceRef);
	}

exit:
	return ret;
}

ca_error BLACKLIST_Add(struct MacAddr *pAddress, struct ca821x_dev *pDeviceRef)
{
#if CASCODA_MAC_BLACKLIST != 0
	ca_error ret;
	if (pAddress->AddressMode != MAC_MODE_LONG_ADDR && pAddress->AddressMode != MAC_MODE_SHORT_ADDR)
	{
		ret = CA_ERROR_INVALID_ARGS;
		goto exit;
	}

	ret = CA_ERROR_NO_BUFFER;
	for (uint8_t i = 0; i < CASCODA_MAC_BLACKLIST; ++i)
	{
		if (pDeviceRef->blacklist[i].AddressMode == MAC_MODE_NO_ADDR)
		{
			pDeviceRef->blacklist[i].AddressMode = pAddress->AddressMode;
			memcpy(pDeviceRef->blacklist[i].Address, pAddress->Address, 8);
			ret = CA_ERROR_SUCCESS;
			goto exit;
		}
	}

exit:
	return ret;
#else
	(void)pAddress;
	(void)pDeviceRef;
	return CA_ERROR_FAIL;
#endif
}

void BLACKLIST_Clear(struct ca821x_dev *pDeviceRef)
{
#if CASCODA_MAC_BLACKLIST != 0
	for (uint8_t i = 0; i < CASCODA_MAC_BLACKLIST; ++i)
	{
		pDeviceRef->blacklist[i].AddressMode = MAC_MODE_NO_ADDR;
	}
#else
	(void)pDeviceRef;
#endif
}
