/*!
* Copyright (c) 2015, Freescale Semiconductor, Inc.
* All rights reserved.
*
* \file Phy.h
*
* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:
*
* o Redistributions of source code must retain the above copyright notice, this list
*   of conditions and the following disclaimer.
*
* o Redistributions in binary form must reproduce the above copyright notice, this
*   list of conditions and the following disclaimer in the documentation and/or
*   other materials provided with the distribution.
*
* o Neither the name of Freescale Semiconductor, Inc. nor the names of its
*   contributors may be used to endorse or promote products derived from this
*   software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
* ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __PHY_H__
#define __PHY_H__


/*! *********************************************************************************
*************************************************************************************
* Include
*************************************************************************************
********************************************************************************** */
#include "EmbeddedTypes.h"
#include "compiler.h"
#include "PhyTypes.h"


/*! *********************************************************************************
*************************************************************************************
* Public macros
*************************************************************************************
********************************************************************************** */
 
#ifdef __cplusplus
    extern "C" {
#endif

#ifndef gSnifferCRCEnabled_d
#define gSnifferCRCEnabled_d          (0)
#endif      

#ifndef gUseStandaloneCCABeforeTx_d
#define gUseStandaloneCCABeforeTx_d   (1)
#endif

#ifndef gPhyRxPBTransferThereshold_d
#define gPhyRxPBTransferThereshold_d  (0)
#endif

/* Reduce the number of XCVR SPI reads/writes to improve Phy response time */
#ifndef gPhyUseReducedSpiAccess_d
#define gPhyUseReducedSpiAccess_d     (1)
#endif 

/* The interval at which the Phy will poll for a new buffer in order to start the RX */
#ifndef gPhyRxRetryInterval_c
#define gPhyRxRetryInterval_c         (60) /* [symbols] */
#endif

//获取临近节点信息表
#ifndef gPhyNeighborTableSize_d
#define gPhyNeighborTableSize_d       (0)
#endif
        
#define PhyGetSeqState()              PhyPpGetState()
#define PhyPlmeForceTrxOffRequest()   PhyAbort()

#define gPhyWarmUpTime_c              (9) /* [symbols] */

#ifndef gPhyMaxIdleRxDuration_c
#define gPhyMaxIdleRxDuration_c      (0xF00000) /* [sym] */
#endif
        
#define ProtectFromXcvrInterrupt()   ProtectFromMCR20Interrupt()
#define UnprotectFromXcvrInterrupt() UnprotectFromMCR20Interrupt()

typedef enum{
    gPhyPwrIdle_c,
    gPhyPwrAutodoze_c,
    gPhyPwrDoze_c,
    gPhyPwrHibernate_c,
    gPhyPwrReset_c
}phyPwrMode_t;

/* XCVR active/idle power modes */
#define gPhyDefaultActivePwrMode_c gPhyPwrAutodoze_c /* Do not change! */
#define gPhyDefaultIdlePwrMode_c   gPhyPwrAutodoze_c

#if gPhyNeighborTableSize_d && !gPhyRxPBTransferThereshold_d
#error The gPhyRxPBTransferThereshold_d must be enabled in order to use the PHY neighbor table!
#endif

/* Set the default power level to 0dBm */
#ifndef gPhyDefaultTxPowerLevel_d
#define gPhyDefaultTxPowerLevel_d     (0x17)
#endif

#define gPhyMaxTxPowerLevel_d         (0x1C)
        
/* Tx Power level limit for each individual channel */
#ifndef gChannelTxPowerLimit_c
#define gChannelTxPowerLimit_c { gPhyMaxTxPowerLevel_d,   /* 11 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 12 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 13 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 14 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 15 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 16 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 17 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 18 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 19 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 20 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 21 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 22 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 23 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 24 */ \
                                 gPhyMaxTxPowerLevel_d,   /* 25 */ \
                                 gPhyMaxTxPowerLevel_d }  /* 26 */
#endif


/*! *********************************************************************************
*************************************************************************************
* Public type definitions
*************************************************************************************
********************************************************************************** */
/* PHY states */
enum {
  gIdle_c=0,
  gRX_c,
  gTX_c,
  gCCA_c,
  gTR_c,
  gCCCA_c,
};

/* PHY channel state */
enum {
  gChannelIdle_c,
  gChannelBusy_c
};

/* PANCORDNTR bit in PP */
enum {
  gMacRole_DeviceOrCoord_c,
  gMacRole_PanCoord_c
};

/* Cca types */
enum {
  gCcaED_c,            /* energy detect - CCA bit not active, not to be used for T and CCCA sequences */
  gCcaCCA_MODE1_c,     /* energy detect - CCA bit ACTIVE */
  gCcaCCA_MODE2_c,     /* 802.15.4 compliant signal detect - CCA bit ACTIVE */
  gCcaCCA_MODE3_c,     /* 802.15.4 compliant signal detect and energy detect - CCA bit ACTIVE */
  gInvalidCcaType_c    /* illegal type */
};

enum {
  gNormalCca_c,
  gContinuousCca_c
};

#define PHY_IRQSTS1_INDEX_c     0x00
#define PHY_IRQSTS2_INDEX_c     0x01
#define PHY_IRQSTS3_INDEX_c     0x02
#define PHY_CTRL1_INDEX_c       0x03
#define PHY_CTRL2_INDEX_c       0x04
#define PHY_CTRL3_INDEX_c       0x05
#define PHY_RX_FRM_LEN_INDEX_c  0x06
#define PHY_CTRL4_INDEX_c       0x07

extern uint8_t mStatusAndControlRegs[9];


/*-------------------------------------------------------*/
void MCR20A_HwInit(UINT8 panNum);
void MCR20A_Abort();
phyStatus_t MCR20A_SetPanId(uint8_t *pPanId, uint8_t pan);
uint8_t MCR20A_SetANTPadStateRequest(bool_t antAB_on, bool_t rxtxSwitch_on);
uint8_t MCR20A_GetState(void);
phyStatus_t MCR20A_SetShortAddr(uint8_t *pShortAddr,uint8_t pan);
phyStatus_t MCR20A_SetLongAddr(uint8_t *pLongAddr,uint8_t pan);
phyStatus_t MCR20A_SetMacRole(bool_t macRole,uint8_t pan);
void MCR20A_SetPromiscuous(bool_t mode);
void MCR20A_SetActivePromiscuous(bool_t state);
bool_t MCR20A_GetActivePromiscuous(void);
void MCR20A_SetSAMState(bool_t state);
void MCR20A_GetRandomNo(uint32_t *pRandomNo);
uint32_t MCR20A_GetMACTimer(void);
void MCR20A_GetIEEEAddr(uint8_t *pLongAddr,uint8_t pan);
phyStatus_t MCR20A_SetCurrentChannelRequest(uint8_t channel,uint8_t pan);
phyStatus_t MCR20A_SetCcaThreshold(uint8_t ccaThreshold);
phyStatus_t MCR20A_SetPwrLevelRequest(uint8_t pwrStep);
phyStatus_t MCR20A_RemoveFromIndirect(uint8_t index);
void MCR20A_SetDualPanSamLvl( uint8_t level );
void MCR20A_IsrSeqCleanup();
uint8_t MCR20A_LqiConvert(uint8_t hwLqi);
uint8_t MCR20A_AES128_Encrypt(uint8_t key[16], uint8_t in[16], uint8_t out[16]);
void MCR20A_ASM_Clear();
void MCR20A_CBC128_Reload();
uint8_t MCR20A_ASM_Selftest();
uint8_t MCR20A_CTR128_Encrypt(uint8_t key[16], uint8_t in[16],uint8_t cnt[16], uint8_t out[16]);



















#ifdef __cplusplus
}
#endif

#endif /* __PHY_H__ */
