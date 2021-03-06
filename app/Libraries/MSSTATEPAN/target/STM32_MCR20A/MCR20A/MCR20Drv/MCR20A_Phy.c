/*! *********************************************************************************
 *************************************************************************************
 * Include
 *************************************************************************************
 ********************************************************************************** */
#include "EmbeddedTypes.h"
#include "MCR20Drv.h"
#include "MCR20Reg.h"
#include "MCR20Overwrites.h"

#include "MCR20A_Phy.h"

/*! *********************************************************************************
 *************************************************************************************
 * Private macros
 *************************************************************************************
 ********************************************************************************** */
#define PHY_MIN_RNG_DELAY 4  /* [symbols] */

/*! *********************************************************************************
 *************************************************************************************
 * Private variables
 *************************************************************************************
 ********************************************************************************** */
const uint8_t gPhyIdlePwrState = gPhyDefaultIdlePwrMode_c;
const uint8_t gPhyActivePwrState = gPhyDefaultActivePwrMode_c;

const uint8_t gPhyIndirectQueueSize_c = 12;
static uint8_t mPhyCurrentSamLvl = 12;
static uint8_t mPhyPwrState = gPhyPwrIdle_c;

#if gPhyNeighborTableSize_d
uint16_t mPhyNeighborTable[gPhyNeighborTableSize_d];
static uint32_t mPhyNeighbotTableUsage = 0;
#endif

/* Mirror XCVR control registers */
uint8_t mStatusAndControlRegs[9];

/*! *********************************************************************************
 *************************************************************************************
 * Public functions
 *************************************************************************************
 ********************************************************************************** */

/*! *********************************************************************************
 * \brief  Initialize the 802.15.4 Radio registers
 * 1.初始化MCR20A对应的SPI及IRQ引脚
 * 2.
 ********************************************************************************** */
void MCR20A_HwInit(UINT8 panNum) {
	uint8_t index;
	uint8_t phyReg;
	phyTime_t timeOut = 0;

	/* Initialize the transceiver SPI driver */
	MCR20Drv_Init();
	//配置
	/* Disable Tristate on COCO MISO for SPI reads */
	MCR20Drv_IndirectAccessSPIWrite((uint8_t) MISC_PAD_CTRL,
			cMISC_PAD_CTRL_NON_GPIO_DS);

	/* PHY_CTRL4 unmask global TRX interrupts, enable 16 bit mode for TC2 - TC2 prime EN */
	mStatusAndControlRegs[PHY_CTRL4] = (gCcaCCA_MODE1_c
			<< cPHY_CTRL4_CCATYPE_Shift_c);
	/* Clear all PP IRQ bits to avoid unexpected interrupts immediately after init,
	 disable all timer interrupts */
	//IRQSTS1~3 中断标志寄存器，用于判断哪个中断发生。写入1清除中断。
	mStatusAndControlRegs[IRQSTS1] = (cIRQSTS1_PLL_UNLOCK_IRQ
			| cIRQSTS1_FILTERFAIL_IRQ | cIRQSTS1_RXWTRMRKIRQ | cIRQSTS1_CCAIRQ
			| cIRQSTS1_RXIRQ | cIRQSTS1_TXIRQ | cIRQSTS1_SEQIRQ);
	//IRQSTS2
	mStatusAndControlRegs[IRQSTS2] = (cIRQSTS2_ASM_IRQ | cIRQSTS2_PB_ERR_IRQ
			| cIRQSTS2_WAKE_IRQ);

	mStatusAndControlRegs[IRQSTS3] = (cIRQSTS3_TMR4MSK | cIRQSTS3_TMR3MSK
			| cIRQSTS3_TMR2MSK | cIRQSTS3_TMR1MSK | cIRQSTS3_TMR4IRQ
			| cIRQSTS3_TMR3IRQ | cIRQSTS3_TMR2IRQ | cIRQSTS3_TMR1IRQ);

	/* PHY_CTRL1 default HW settings  + AUTOACK enabled */
	mStatusAndControlRegs[PHY_CTRL1] = cPHY_CTRL1_AUTOACK ;//	默认不打开，打开CCA会导致ACK丢包| cPHY_CTRL1_CCABFRTX;

	/* PHY_CTRL2 : disable all interrupts */
	mStatusAndControlRegs[PHY_CTRL2] = (cPHY_CTRL2_CRC_MSK
			| cPHY_CTRL2_PLL_UNLOCK_MSK | cPHY_CTRL2_FILTERFAIL_MSK
			| cPHY_CTRL2_RX_WMRK_MSK | cPHY_CTRL2_CCAMSK | cPHY_CTRL2_RXMSK
			| cPHY_CTRL2_TXMSK | cPHY_CTRL2_SEQMSK);

#if  1   //def SEQ_MRK_ENABLE
	//开启RX中断
	mStatusAndControlRegs[PHY_CTRL2] &= ~(cPHY_CTRL2_SEQMSK);
#else
	//开启TX,RX_WMRK中断
	mStatusAndControlRegs[PHY_CTRL2] &= ~(cPHY_CTRL2_TXMSK | cPHY_CTRL2_RX_WMRK_MSK);
#endif

	/* PHY_CTRL3 : disable all timers and remaining interrupts */
	mStatusAndControlRegs[PHY_CTRL3] = (cPHY_CTRL3_ASM_MSK
			| cPHY_CTRL3_PB_ERR_MSK | cPHY_CTRL3_WAKE_MSK
#if gPhyUseReducedSpiAccess_d
			/* Enable all TMR comparators */
			| cPHY_CTRL3_TMR1CMP_EN | cPHY_CTRL3_TMR2CMP_EN
			| cPHY_CTRL3_TMR3CMP_EN | cPHY_CTRL3_TMR4CMP_EN
#endif
			);

	/* SRC_CTRL */
	mStatusAndControlRegs[SRC_CTRL]=0;	//关闭cSRC_CTRL_ACK_FRM_PND和cSRC_CTRL_SRCADDR_EN
	//mStatusAndControlRegs[SRC_CTRL] = (cSRC_CTRL_ACK_FRM_PND
	//		| cSRC_CTRL_SRCADDR_EN
		//	| (cSRC_CTRL_INDEX << cSRC_CTRL_INDEX_Shift_c));

	/* Write settings in XCVR */
	MCR20Drv_DirectAccessSPIMultiByteWrite(IRQSTS1, mStatusAndControlRegs,
			sizeof(mStatusAndControlRegs));

	/*  RX_FRAME_FILTER
	 FRM_VER[1:0] = b11. Accept FrameVersion 0 and 1 packets, reject all others */
	MCR20Drv_IndirectAccessSPIWrite(RX_FRAME_FILTER,
			(uint8_t)(
					cRX_FRAME_FLT_FRM_VER | cRX_FRAME_FLT_BEACON_FT
							| cRX_FRAME_FLT_DATA_FT | cRX_FRAME_FLT_CMD_FT));
	/* Direct register overwrites */
	for (index = 0; index < sizeof(overwrites_direct) / sizeof(overwrites_t);
			index++)
		MCR20Drv_DirectAccessSPIWrite(overwrites_direct[index].address,
				overwrites_direct[index].data);

	/* Indirect register overwrites */
	for (index = 0; index < sizeof(overwrites_indirect) / sizeof(overwrites_t);
			index++)
		MCR20Drv_IndirectAccessSPIWrite(overwrites_indirect[index].address,
				overwrites_indirect[index].data);

	/* Clear HW indirect queue */
	for (index = 0; index < gPhyIndirectQueueSize_c; index++)
		MCR20A_RemoveFromIndirect(index);

	MCR20A_SetCurrentChannelRequest(0x0B, panNum); /* 2405 MHz */
#if 0 //双PAN支持
	MCR20A_SetCurrentChannelRequest(0x0B, 1); /* 2405 MHz */

	/* Split the HW Indirect hash table in two */
	MCR20A_SetDualPanSamLvl( gPhyIndirectQueueSize_c / 2 );
#else
	/* Assign HW Indirect hash table to PAN0 */
	MCR20A_SetDualPanSamLvl(gPhyIndirectQueueSize_c);
#endif

	/* Set the default Tx power level */
	MCR20A_SetPwrLevelRequest(gPhyDefaultTxPowerLevel_d);
	/* Set CCA threshold to -75 dBm */
	MCR20A_SetCcaThreshold(0x4B);
	/* Set prescaller to obtain 1 symbol (16us) timebase */
	MCR20Drv_IndirectAccessSPIWrite(TMR_PRESCALE, 0x05);
	/* Write default Rx watermark level */
	MCR20Drv_IndirectAccessSPIWrite(RX_WTR_MARK, 0);

	//开启TX/RX_switch
	//MCR20A_SetANTPadStateRequest((bool_t) 0, (bool_t) 1);

#if 0
	/* enable autodoze mode. */
	phyReg = MCR20Drv_DirectAccessSPIRead((uint8_t) PWR_MODES);
	phyReg |= (uint8_t) cPWR_MODES_AUTODOZE;
	MCR20Drv_DirectAccessSPIWrite((uint8_t) PWR_MODES, phyReg);
#endif

#if gPhyNeighborTableSize_d
	mPhyNeighbotTableUsage = 0;
#endif

	//设置输出频率为16Mhz
	MCR20Drv_Set_CLK_OUT_Freq(gCLK_OUT_FREQ_16_MHz);

	int delay_count = 10000;
	while (delay_count--) {
		//延迟一段时间保证不会误触发中断
	}

	//等待空闲状态
	while (MCR20Drv_DirectAccessSPIRead(SEQ_STATE) & 0x1F) {
	}

	//清中断
	MCR20Drv_IRQ_Clear();
	//初始化结束，开启中断
	MCR20Drv_IRQ_Enable();
}

/*! *********************************************************************************
 * \brief  Aborts the current sequence and force the radio to IDLE
 *
 ********************************************************************************** */
void MCR20A_Abort() {
	volatile uint8_t currentTime = 0;

	/* Mask XCVR irq */
	MCR20Drv_IRQ_Disable();

	/* Read SCVR status and control registers: IRQSTS1-IRQSTS3, PHY_CTRL1 */
#if gPhyUseReducedSpiAccess_d
	mStatusAndControlRegs[0] = MCR20Drv_DirectAccessSPIMultiByteRead(IRQSTS2,
			&mStatusAndControlRegs[1], 3);
#else
	mStatusAndControlRegs[0] = MCR20Drv_DirectAccessSPIMultiByteRead(IRQSTS2, &mStatusAndControlRegs[1], 8);
#endif

	/* Mask SEQ interrupt */
	mStatusAndControlRegs[PHY_CTRL2] |= (cPHY_CTRL2_SEQMSK);
#if gPhyUseReducedSpiAccess_d
	MCR20Drv_DirectAccessSPIWrite(PHY_CTRL2, mStatusAndControlRegs[PHY_CTRL2]);
#else
	/* Stop timers */
	mStatusAndControlRegs[PHY_CTRL3] &= ~(cPHY_CTRL3_TMR2CMP_EN | cPHY_CTRL3_TMR3CMP_EN);
	mStatusAndControlRegs[PHY_CTRL4] &= ~(cPHY_CTRL4_TC3TMOUT);
	MCR20Drv_DirectAccessSPIMultiByteWrite(PHY_CTRL2, &mStatusAndControlRegs[PHY_CTRL2], 4);
#endif

	/* Disable timer trigger (for scheduled XCVSEQ) */
	if (mStatusAndControlRegs[PHY_CTRL1] & cPHY_CTRL1_TMRTRIGEN) {
		mStatusAndControlRegs[PHY_CTRL1] &= ~(cPHY_CTRL1_TMRTRIGEN);
		MCR20Drv_DirectAccessSPIWrite(PHY_CTRL1,
				mStatusAndControlRegs[PHY_CTRL1]);

		/* Give the FSM enough time to start if it was triggered */
		currentTime = (MCR20Drv_DirectAccessSPIRead(EVENT_TMR_LSB) + 2);
		while (MCR20Drv_DirectAccessSPIRead(EVENT_TMR_LSB) != currentTime)
			;
	}

	if ((mStatusAndControlRegs[PHY_CTRL1] & cPHY_CTRL1_XCVSEQ) != gIdle_c) {
		/* Abort current SEQ */
		mStatusAndControlRegs[PHY_CTRL1] &= ~(cPHY_CTRL1_XCVSEQ);
		MCR20Drv_DirectAccessSPIWrite(PHY_CTRL1,
				mStatusAndControlRegs[PHY_CTRL1]);

		/* Wait for Sequence Idle (if not already) */
		while ((MCR20Drv_DirectAccessSPIRead(SEQ_STATE) & 0x1F) != 0)
			;
	}

	/* Clear all PP IRQ bits to avoid unexpected interrupts and mask TMR3 interrupt.
	 Do not change TMR IRQ status. */
	mStatusAndControlRegs[IRQSTS3] &= 0xF0;
	mStatusAndControlRegs[IRQSTS3] |= (cIRQSTS3_TMR3MSK | cIRQSTS3_TMR2MSK
			| cIRQSTS3_TMR2IRQ | cIRQSTS3_TMR3IRQ);

	/* write all registers with a single SPI burst write */
	MCR20Drv_DirectAccessSPIMultiByteWrite(IRQSTS1, mStatusAndControlRegs, 3);

	//PhyIsrPassRxParams(NULL);

#if gPhyRxPBTransferThereshold_d
	MCR20Drv_IndirectAccessSPIWrite(RX_WTR_MARK, 0);
#endif

	/* Unmask XCVR irq */
	MCR20Drv_IRQ_Enable();
}

/*! *********************************************************************************
 * \brief  Get the state of the ZLL
 *
 * \return  uint8_t state
 *
 ********************************************************************************** */
uint8_t MCR20A_GetState(void) {
#if gPhyUseReducedSpiAccess_d   //记录状态，降低SPI读写次数，提高执行效率
	return mStatusAndControlRegs[PHY_CTRL1] & cPHY_CTRL1_XCVSEQ;
#else
	return (uint8_t)( MCR20Drv_DirectAccessSPIRead( (uint8_t) PHY_CTRL1) & cPHY_CTRL1_XCVSEQ );
#endif
}

/*! *********************************************************************************
 * \brief  Set the value of the MAC PanId
 *
 * \param[in]  pPanId
 * \param[in]  pan
 *
 * \return  phyStatus_t
 *
 ********************************************************************************** */
phyStatus_t MCR20A_SetPanId(uint8_t *pPanId, uint8_t pan) {
#ifdef PHY_PARAMETERS_VALIDATION
	if(NULL == pPanId) {
		return gPhyInvalidParameter_c;
	}
#endif /* PHY_PARAMETERS_VALIDATION */

	if (0 == pan)
		MCR20Drv_IndirectAccessSPIMultiByteWrite((uint8_t) MACPANID0_LSB,
				pPanId, 2);
	else
		MCR20Drv_IndirectAccessSPIMultiByteWrite((uint8_t) MACPANID1_LSB,
				pPanId, 2);

	return gPhySuccess_c;
}

/*! *********************************************************************************
 * \brief  Set the value of the MAC Short Address
 *
 * \param[in]  pShortAddr
 * \param[in]  pan
 *
 * \return  phyStatus_t
 *
 ********************************************************************************** */
phyStatus_t MCR20A_SetShortAddr(uint8_t *pShortAddr, uint8_t pan) {
#ifdef PHY_PARAMETERS_VALIDATION
	if(NULL == pShortAddr) {
		return gPhyInvalidParameter_c;
	}
#endif /* PHY_PARAMETERS_VALIDATION */

	if (pan == 0) {
		MCR20Drv_IndirectAccessSPIMultiByteWrite((uint8_t) MACSHORTADDRS0_LSB,
				pShortAddr, 2);
	} else {
		MCR20Drv_IndirectAccessSPIMultiByteWrite((uint8_t) MACSHORTADDRS1_LSB,
				pShortAddr, 2);
	}

	return gPhySuccess_c;
}

/*! *********************************************************************************
 * \brief  Set the value of the MAC extended address
 *
 * \param[in]  pLongAddr
 * \param[in]  pan
 *
 * \return  phyStatus_t
 *
 ********************************************************************************** */
phyStatus_t MCR20A_SetLongAddr(uint8_t *pLongAddr, uint8_t pan) {
#ifdef PHY_PARAMETERS_VALIDATION
	if(NULL == pLongAddr) {
		return gPhyInvalidParameter_c;
	}
#endif /* PHY_PARAMETERS_VALIDATION */

	if (0 == pan)
		MCR20Drv_IndirectAccessSPIMultiByteWrite((uint8_t) MACLONGADDRS0_0,
				pLongAddr, 8);
	else
		MCR20Drv_IndirectAccessSPIMultiByteWrite((uint8_t) MACLONGADDRS1_0,
				pLongAddr, 8);

	return gPhySuccess_c;
}

/*! *********************************************************************************
 * \brief  Set the MAC PanCoordinator role
 *
 * \param[in]  macRole
 * \param[in]  pan
 *
 * \return  phyStatus_t
 *
 ********************************************************************************** */
phyStatus_t MCR20A_SetMacRole(bool_t macRole, uint8_t pan) {
	uint8_t phyReg;

	if (0 == pan) {
#if gPhyUseReducedSpiAccess_d
		phyReg = mStatusAndControlRegs[PHY_CTRL4];
#else
		phyReg = MCR20Drv_DirectAccessSPIRead( (uint8_t) PHY_CTRL4);
#endif

		if (gMacRole_PanCoord_c == macRole) {
			phyReg |= cPHY_CTRL4_PANCORDNTR0;
		} else {
			phyReg &= ~cPHY_CTRL4_PANCORDNTR0;
		}
#if gPhyUseReducedSpiAccess_d
		mStatusAndControlRegs[PHY_CTRL4] = phyReg;
#endif
		MCR20Drv_DirectAccessSPIWrite((uint8_t) PHY_CTRL4, phyReg);
	} else {
		phyReg = MCR20Drv_IndirectAccessSPIRead((uint8_t) DUAL_PAN_CTRL);

		if (gMacRole_PanCoord_c == macRole) {
			phyReg |= cDUAL_PAN_CTRL_PANCORDNTR1;
		} else {
			phyReg &= ~cDUAL_PAN_CTRL_PANCORDNTR1;
		}
		MCR20Drv_IndirectAccessSPIWrite((uint8_t) DUAL_PAN_CTRL, phyReg);
	}

	return gPhySuccess_c;
}

/*! *********************************************************************************
 * \brief  Set the PHY in Promiscuous mode
 *
 * \param[in]  mode
 *
 ********************************************************************************** */
void MCR20A_SetPromiscuous(bool_t mode) {
	uint8_t rxFrameFltReg, phyCtrl4Reg;

	rxFrameFltReg = MCR20Drv_IndirectAccessSPIRead((uint8_t) RX_FRAME_FILTER);
#if gPhyUseReducedSpiAccess_d
	phyCtrl4Reg = mStatusAndControlRegs[PHY_CTRL4];
#else
	phyCtrl4Reg = MCR20Drv_DirectAccessSPIRead( (uint8_t) PHY_CTRL4);
#endif

	if (mode) {
		/* FRM_VER[1:0] = b00. 00: Any FrameVersion accepted (0,1,2 & 3) */
		/* All frame types accepted*/
		phyCtrl4Reg |= cPHY_CTRL4_PROMISCUOUS;
		rxFrameFltReg &= ~(cRX_FRAME_FLT_FRM_VER);
		rxFrameFltReg |= (cRX_FRAME_FLT_ACK_FT | cRX_FRAME_FLT_NS_FT);
	} else {
		phyCtrl4Reg &= ~cPHY_CTRL4_PROMISCUOUS;
		/* FRM_VER[1:0] = b11. Accept FrameVersion 0 and 1 packets, reject all others */
		/* Beacon, Data and MAC command frame types accepted */
		rxFrameFltReg &= ~(cRX_FRAME_FLT_FRM_VER);
		rxFrameFltReg |= (0x03 << cRX_FRAME_FLT_FRM_VER_Shift_c);
		rxFrameFltReg &= ~(cRX_FRAME_FLT_ACK_FT | cRX_FRAME_FLT_NS_FT);
	}
#if gPhyUseReducedSpiAccess_d
	mStatusAndControlRegs[PHY_CTRL4] = phyCtrl4Reg;
#endif
	MCR20Drv_IndirectAccessSPIWrite((uint8_t) RX_FRAME_FILTER, rxFrameFltReg);
	MCR20Drv_DirectAccessSPIWrite((uint8_t) PHY_CTRL4, phyCtrl4Reg);
}

/*! *********************************************************************************
 * \brief  Set the PHY in ActivePromiscuous mode
 *
 * \param[in]  state
 *
 ********************************************************************************** */
void MCR20A_SetActivePromiscuous(bool_t state) {
	uint8_t phyCtrl4Reg;
	uint8_t phyFrameFilterReg;

#if gPhyUseReducedSpiAccess_d
	phyCtrl4Reg = mStatusAndControlRegs[PHY_CTRL4];
#else
	phyCtrl4Reg = MCR20Drv_DirectAccessSPIRead( (uint8_t) PHY_CTRL4);
#endif
	phyFrameFilterReg = MCR20Drv_IndirectAccessSPIRead(RX_FRAME_FILTER);

	/* if Prom is set */
	if (state) {
		if (phyCtrl4Reg & cPHY_CTRL4_PROMISCUOUS) {
			/* Disable Promiscuous mode */
			phyCtrl4Reg &= ~(cPHY_CTRL4_PROMISCUOUS);

			/* Enable Active Promiscuous mode */
			phyFrameFilterReg |= cRX_FRAME_FLT_ACTIVE_PROMISCUOUS;
		}
	} else {
		if (phyFrameFilterReg & cRX_FRAME_FLT_ACTIVE_PROMISCUOUS) {
			/* Disable Active Promiscuous mode */
			phyFrameFilterReg &= ~(cRX_FRAME_FLT_ACTIVE_PROMISCUOUS);

			/* Enable Promiscuous mode */
			phyCtrl4Reg |= cPHY_CTRL4_PROMISCUOUS;
		}
	}
#if gPhyUseReducedSpiAccess_d
	mStatusAndControlRegs[PHY_CTRL4] = phyCtrl4Reg;
#endif
	MCR20Drv_DirectAccessSPIWrite((uint8_t) PHY_CTRL4, phyCtrl4Reg);
	MCR20Drv_IndirectAccessSPIWrite(RX_FRAME_FILTER, phyFrameFilterReg);
}

/*! *********************************************************************************
 * \brief  Get the state of the ActivePromiscuous mode
 *
 * \return  bool_t state
 *
 ********************************************************************************** */
bool_t MCR20A_GetActivePromiscuous(void) {
	uint8_t phyReg = MCR20Drv_IndirectAccessSPIRead(RX_FRAME_FILTER);

	if (phyReg & cRX_FRAME_FLT_ACTIVE_PROMISCUOUS)
		return TRUE;

	return FALSE;
}

/*! *********************************************************************************
 * \brief  Set the state of the SAM HW module
 *
 * \param[in]  state
 *
 ********************************************************************************** */
void MCR20A_SetSAMState(bool_t state) {
	uint8_t phyReg, newPhyReg;
	/* Disable/Enables the Source Address Matching feature */
#if gPhyUseReducedSpiAccess_d
	phyReg = mStatusAndControlRegs[SRC_CTRL];
#else
	phyReg = MCR20Drv_DirectAccessSPIRead(SRC_CTRL);
#endif
	if (state)
		newPhyReg = phyReg | cSRC_CTRL_SRCADDR_EN;
	else
		newPhyReg = phyReg & ~(cSRC_CTRL_SRCADDR_EN);

	if (newPhyReg != phyReg) {
		MCR20Drv_DirectAccessSPIWrite(SRC_CTRL, newPhyReg);
#if gPhyUseReducedSpiAccess_d
		mStatusAndControlRegs[SRC_CTRL] = newPhyReg;
#endif
	}
}

/*! *********************************************************************************
 * \brief  Add a new element to the PHY indirect queue
 *
 * \param[in]  index
 * \param[in]  checkSum
 * \param[in]  instanceId
 *
 * \return  phyStatus_t
 *
 ********************************************************************************** */
phyStatus_t MCR20A_IndirectQueueInsert(uint8_t index, uint16_t checkSum,
		int instanceId) {
	uint8_t srcCtrlReg;

	if (index >= gPhyIndirectQueueSize_c)
		return gPhyInvalidParameter_c;

	srcCtrlReg = (index & cSRC_CTRL_INDEX) << cSRC_CTRL_INDEX_Shift_c;
	MCR20Drv_DirectAccessSPIWrite(SRC_CTRL, srcCtrlReg);

	MCR20Drv_DirectAccessSPIMultiByteWrite(SRC_ADDRS_SUM_LSB,
			(uint8_t *) &checkSum, 2);

	srcCtrlReg |= cSRC_CTRL_SRCADDR_EN | cSRC_CTRL_INDEX_EN;
	MCR20Drv_DirectAccessSPIWrite(SRC_CTRL, srcCtrlReg);

	return gPhySuccess_c;
}

/*! *********************************************************************************
 * \brief  Return TRUE if the received packet is a PollRequest
 *
 * \return  bool_t
 *
 ********************************************************************************** */
bool_t MCR20A_IsPollIndication(void) {
	uint8_t irqsts2Reg;
	irqsts2Reg = MCR20Drv_DirectAccessSPIRead((uint8_t) IRQSTS2);
	if (irqsts2Reg & cIRQSTS2_PI) {
		return TRUE;
	}
	return FALSE;
}

/*! *********************************************************************************
 * \brief  Return the state of the FP bit of the received ACK
 *
 * \return  bool_t
 *
 ********************************************************************************** */
bool_t MCR20A_IsRxAckDataPending(void) {
	uint8_t irqsts1Reg;
	irqsts1Reg = MCR20Drv_DirectAccessSPIRead((uint8_t) IRQSTS1);
	if (irqsts1Reg & cIRQSTS1_RX_FRM_PEND) {
		return TRUE;
	}
	return FALSE;
}

/*! *********************************************************************************
 * \brief  Return TRUE if there is data pending for the Poling Device
 *
 * \return  bool_t
 *
 ********************************************************************************** */
bool_t MCR20A_IsTxAckDataPending(void) {
	uint8_t srcCtrlReg;
#if gPhyUseReducedSpiAccess_d
	srcCtrlReg = mStatusAndControlRegs[SRC_CTRL];
#else
	srcCtrlReg = MCR20Drv_DirectAccessSPIRead(SRC_CTRL);
#endif
	if (srcCtrlReg & cSRC_CTRL_SRCADDR_EN) {
		uint8_t irqsts2Reg;

		irqsts2Reg = MCR20Drv_DirectAccessSPIRead((uint8_t) IRQSTS2);

		if (irqsts2Reg & cIRQSTS2_SRCADDR)
			return TRUE;
		else
			return FALSE;
	} else {
		return ((srcCtrlReg & cSRC_CTRL_ACK_FRM_PND) == cSRC_CTRL_ACK_FRM_PND);
	}
}

/*! *********************************************************************************
 * \brief  Set the value of the CCA threshold
 *
 * \param[in]  ccaThreshold
 *
 * \return  phyStatus_t
 *
 ********************************************************************************** */
phyStatus_t MCR20A_SetCcaThreshold(uint8_t ccaThreshold) {
	MCR20Drv_IndirectAccessSPIWrite((uint8_t) CCA1_THRESH,
			(uint8_t) ccaThreshold);
	return gPhySuccess_c;
}

/*! *********************************************************************************
 * \brief  This function will set the value for the FAD threshold
 *
 * \param[in]  FADThreshold   the FAD threshold
 *
 * \return  phyStatus_t
 *
 ********************************************************************************** */
uint8_t MCR20A_SetFADThresholdRequest(uint8_t FADThreshold) {
	MCR20Drv_IndirectAccessSPIWrite(FAD_THR, FADThreshold);
	return gPhySuccess_c;
}

/*! *********************************************************************************
 * \brief  This function will enable/disable the FAD
 *
 * \param[in]  state   the state of the FAD
 *
 * \return  phyStatus_t
 *
 ********************************************************************************** */
uint8_t MCR20A_SetFADStateRequest(bool_t state) {
	uint8_t phyReg;

	phyReg = MCR20Drv_IndirectAccessSPIRead(ANT_AGC_CTRL);
	state ? (phyReg |= cANT_AGC_CTRL_FAD_EN_Mask_c) : (phyReg &=
					(~((uint8_t)cANT_AGC_CTRL_FAD_EN_Mask_c)));
	MCR20Drv_IndirectAccessSPIWrite(ANT_AGC_CTRL, phyReg);

	phyReg = MCR20Drv_IndirectAccessSPIRead(ANT_PAD_CTRL);
	state ? (phyReg |= 0x02) : (phyReg &= ~cANT_PAD_CTRL_ANTX_EN);
	MCR20Drv_IndirectAccessSPIWrite(ANT_PAD_CTRL, phyReg);

	return gPhySuccess_c;
}

/*! *********************************************************************************
 * \brief  This function will set the LQI mode
 *
 * \return  uint8_t
 *
 ********************************************************************************** */
uint8_t MCR20A_SetLQIModeRequest(uint8_t lqiMode) {
	uint8_t currentMode;

	currentMode = MCR20Drv_IndirectAccessSPIRead(CCA_CTRL);
	lqiMode ? (currentMode |= cCCA_CTRL_LQI_RSSI_NOT_CORR) : (currentMode &=
						(~((uint8_t)cCCA_CTRL_LQI_RSSI_NOT_CORR)));
	MCR20Drv_IndirectAccessSPIWrite(CCA_CTRL, currentMode);

	return gPhySuccess_c;
}

/*! *********************************************************************************
 * \brief  This function will return the RSSI level
 *
 * \return  uint8_t
 *
 ********************************************************************************** */
uint8_t MCR20A_GetRSSILevelRequest(void) {
	return MCR20Drv_IndirectAccessSPIRead(RSSI);
}

/*! *********************************************************************************
 * \brief  This function will enable/disable the ANTX
 *
 * \param[in]  state   the state of the ANTX
 *
 * \return  phyStatus_t
 *
 ********************************************************************************** */
uint8_t MCR20A_SetANTXStateRequest(bool_t state) {
	uint8_t phyReg;

	phyReg = MCR20Drv_IndirectAccessSPIRead(ANT_AGC_CTRL);
	state ? (phyReg |= cANT_AGC_CTRL_ANTX_Mask_c) : (phyReg &=
					(~((uint8_t)cANT_AGC_CTRL_ANTX_Mask_c)));
	MCR20Drv_IndirectAccessSPIWrite(ANT_AGC_CTRL, phyReg);

	return gPhySuccess_c;
}

/*! *********************************************************************************
 * \brief  This function will retrn the state of the ANTX
 *
 * \return  uint8_t
 *
 ********************************************************************************** */
uint8_t MCR20A_GetANTXStateRequest(void) {
	uint8_t phyReg;

	phyReg = MCR20Drv_IndirectAccessSPIRead(ANT_AGC_CTRL);

	return ((phyReg & cANT_AGC_CTRL_ANTX_Mask_c) == cANT_AGC_CTRL_ANTX_Mask_c);
}

/*! *********************************************************************************
 * \brief  Set the state of the Dual Pan Auto mode
 *
 * \param[in]  mode TRUE/FALSE
 *
 ********************************************************************************** */
void MCR20A_SetDualPanAuto(bool_t mode) {
	uint8_t phyReg, phyReg2;

	phyReg = MCR20Drv_IndirectAccessSPIRead((uint8_t) DUAL_PAN_CTRL);

	if (mode) {
		phyReg2 = phyReg | (cDUAL_PAN_CTRL_DUAL_PAN_AUTO);
	} else {
		phyReg2 = phyReg & (~cDUAL_PAN_CTRL_DUAL_PAN_AUTO);
	}

	/* Write the new value only if it has changed */
	if (phyReg2 != phyReg)
		MCR20Drv_IndirectAccessSPIWrite((uint8_t) DUAL_PAN_CTRL, phyReg2);
}

/*! *********************************************************************************
 * \brief  Get the state of the Dual Pan Auto mode
 *
 * \return  bool_t state
 *
 ********************************************************************************** */
bool_t MCR20A_GetDualPanAuto(void) {
	uint8_t phyReg = MCR20Drv_IndirectAccessSPIRead(DUAL_PAN_CTRL);
	return (phyReg & cDUAL_PAN_CTRL_DUAL_PAN_AUTO)
			== cDUAL_PAN_CTRL_DUAL_PAN_AUTO;
}

/*! *********************************************************************************
 * \brief  Set the dwell for the Dual Pan Auto mode
 *
 * \param[in]  dwell
 *
 ********************************************************************************** */
void MCR20A_SetDualPanDwell(uint8_t dwell) {
	MCR20Drv_IndirectAccessSPIWrite((uint8_t) DUAL_PAN_DWELL, dwell);
}

/*! *********************************************************************************
 * \brief  Get the dwell for the Dual Pan Auto mode
 *
 * \return  uint8_t PAN dwell
 *
 ********************************************************************************** */
uint8_t MCR20A_GetDualPanDwell(void) {
	return MCR20Drv_IndirectAccessSPIRead((uint8_t) DUAL_PAN_DWELL);
}

/*! *********************************************************************************
 * \brief  Get the remeining time before a PAN switch occures
 *
 * \return  uint8_t remaining time
 *
 ********************************************************************************** */
uint8_t MCR20A_GetDualPanRemain(void) {
	return (MCR20Drv_IndirectAccessSPIRead(DUAL_PAN_STS)
			& cDUAL_PAN_STS_DUAL_PAN_REMAIN);
}

/*! *********************************************************************************
 * \brief  Set the current active Nwk
 *
 * \param[in]  nwk index of the nwk
 *
 ********************************************************************************** */
void MCR20A_SetDualPanActiveNwk(uint8_t nwk) {
	uint8_t phyReg, phyReg2;

	phyReg = MCR20Drv_IndirectAccessSPIRead((uint8_t) DUAL_PAN_CTRL);

	if (0 == nwk) {
		phyReg2 = phyReg & (~cDUAL_PAN_CTRL_ACTIVE_NETWORK);
	} else {
		phyReg2 = phyReg | cDUAL_PAN_CTRL_ACTIVE_NETWORK;
	}

	/* Write the new value only if it has changed */
	if (phyReg2 != phyReg) {
		MCR20Drv_IndirectAccessSPIWrite((uint8_t) DUAL_PAN_CTRL, phyReg2);
	}
}

/*! *********************************************************************************
 * \brief  Return the index of the Acive PAN
 *
 * \return  uint8_t index
 *
 ********************************************************************************** */
uint8_t MCR20A_GetDualPanActiveNwk(void) {
	uint8_t phyReg;

	phyReg = MCR20Drv_IndirectAccessSPIRead((uint8_t) DUAL_PAN_CTRL);

	return (phyReg & cDUAL_PAN_CTRL_CURRENT_NETWORK) > 0;
}

/*! *********************************************************************************
 * \brief  Returns the PAN bitmask for the last Rx packet.
 *         A packet can be received on multiple PANs
 *
 * \return  uint8_t bitmask
 *
 ********************************************************************************** */
uint8_t MCR20A_GetPanOfRxPacket(void) {
	uint8_t phyReg;
	uint8_t PanBitMask = 0;

	phyReg = MCR20Drv_IndirectAccessSPIRead((uint8_t) DUAL_PAN_STS);

	if (phyReg & cDUAL_PAN_STS_RECD_ON_PAN0)
		PanBitMask |= (1 << 0);

	if (phyReg & cDUAL_PAN_STS_RECD_ON_PAN1)
		PanBitMask |= (1 << 1);

	return PanBitMask;
}

/*! *********************************************************************************
 * \brief  Get the indirect queue level at which the HW queue will be split between PANs
 *
 * \return  uint8_t level
 *
 ********************************************************************************** */
uint8_t MCR20A_GetDualPanSamLvl(void) {
	return mPhyCurrentSamLvl;
}

/*! *********************************************************************************
 * \brief  Set the indirect queue level at which the HW queue will be split between PANs
 *
 * \param[in]  level
 *
 ********************************************************************************** */
void MCR20A_SetDualPanSamLvl(uint8_t level) {
	uint8_t phyReg;
#ifdef PHY_PARAMETERS_VALIDATION
	if( level > gPhyIndirectQueueSize_c )
	return;
#endif
	phyReg = MCR20Drv_IndirectAccessSPIRead((uint8_t) DUAL_PAN_CTRL);

	phyReg &= ~cDUAL_PAN_CTRL_DUAL_PAN_SAM_LVL_MSK; /* Clear current lvl */
	phyReg |= level << cDUAL_PAN_CTRL_DUAL_PAN_SAM_LVL_Shift_c; /* Set new lvl */

	MCR20Drv_IndirectAccessSPIWrite((uint8_t) DUAL_PAN_CTRL, phyReg);
	mPhyCurrentSamLvl = level;
}

/*! *********************************************************************************
 * \brief  Change the XCVR power state
 *
 * \param[in]  state  the new XCVR power state
 *
 * \return  phyStatus_t
 *
 * \pre Before entering hibernate/reset states, the MCG clock source must be changed
 *      to use an input other than the one generated by the XCVR!
 *
 * \post When XCVR is in hibernate, indirect registers cannot be accessed in burst mode
 *       When XCVR is in reset, all registers are inaccessible!
 *
 * \remarks Putting the XCVR into hibernate/reset will stop the generated clock signal!
 *
 ********************************************************************************** */
phyStatus_t MCR20A_SetPwrState(uint8_t state) {
	uint8_t phyPWR, xtalState;

	/* Parameter validation */
	if (state > gPhyPwrReset_c)
		return gPhyInvalidParameter_c;

	/* Check if the new power state = old power state */
	if (state == mPhyPwrState)
		return gPhyBusy_c;

#if 0		//不支持复位
	/* Check if the XCVR is in reset power mode */
	if( mPhyPwrState == gPhyPwrReset_c ) {
		MCR20Drv_RST_B_Deassert();
		/* Wait for transceiver to deassert IRQ pin */
		while( MCR20Drv_IsIrqPending() );
		/* Wait for transceiver wakeup from POR iterrupt */
		while( !MCR20Drv_IsIrqPending() );
		/* After reset, the radio is in Idle state */
		mPhyPwrState = gPhyPwrIdle_c;
		/* Restore default radio settings */
		MCR20A_HwInit(0);
	}
#endif

	if (state != gPhyPwrReset_c) {
		phyPWR = MCR20Drv_DirectAccessSPIRead(PWR_MODES);
		xtalState = phyPWR & cPWR_MODES_XTALEN;
	}

	switch (state) {
	case gPhyPwrIdle_c:
		phyPWR &= ~(cPWR_MODES_AUTODOZE);
		phyPWR |= (cPWR_MODES_XTALEN | cPWR_MODES_PMC_MODE);
		break;

	case gPhyPwrAutodoze_c:
		phyPWR |=
				(cPWR_MODES_XTALEN | cPWR_MODES_AUTODOZE | cPWR_MODES_PMC_MODE);
		break;

	case gPhyPwrDoze_c:
		phyPWR &= ~(cPWR_MODES_AUTODOZE | cPWR_MODES_PMC_MODE);
		phyPWR |= cPWR_MODES_XTALEN;
		break;

	case gPhyPwrHibernate_c:
		phyPWR &= ~(cPWR_MODES_XTALEN | cPWR_MODES_AUTODOZE
				| cPWR_MODES_PMC_MODE);
		break;

	case gPhyPwrReset_c:
		MCR20Drv_IRQ_Disable();
		mPhyPwrState = gPhyPwrReset_c;
		MCR20Drv_RST_B_Assert();
		return gPhySuccess_c;
	}

	mPhyPwrState = state;
	MCR20Drv_DirectAccessSPIWrite(PWR_MODES, phyPWR);

	if (!xtalState && (phyPWR & cPWR_MODES_XTALEN)) {
		/* wait for crystal oscillator to complet its warmup */
		while ((MCR20Drv_DirectAccessSPIRead(PWR_MODES) & cPWR_MODES_XTAL_READY)
				!= cPWR_MODES_XTAL_READY)
			;
		/* wait for radio wakeup from hibernate interrupt */
		while ((MCR20Drv_DirectAccessSPIRead(IRQSTS2)
				& (cIRQSTS2_WAKE_IRQ | cIRQSTS2_TMRSTATUS))
				!= (cIRQSTS2_WAKE_IRQ | cIRQSTS2_TMRSTATUS))
			;

		MCR20Drv_DirectAccessSPIWrite(IRQSTS2, cIRQSTS2_WAKE_IRQ);
	}

	return gPhySuccess_c;
}

/*! *********************************************************************************
 * \brief  Get a random number from the XCVR
 *
 * \param[in]  pRandomNo  pointer to a location where the random number will be stored
 *
 * \pre This function should be called only when the Radio is idle.
 *      The function may take a long time to run (more than 16 symbols)!
 *      It is recomended to use this function only to initializa a seed at startup!
 *
 ********************************************************************************** */
void MCR20A_GetRandomNo(uint32_t *pRandomNo) {
	uint8_t i = 4; //4字节
	uint8_t* ptr = (uint8_t *) pRandomNo;
	uint8_t phyReg;

	MCR20Drv_IRQ_Disable();

	/* Check if XCVR is idle */
	phyReg = MCR20Drv_DirectAccessSPIRead(PHY_CTRL1);

	if ((phyReg & cPHY_CTRL1_XCVSEQ) == gIdle_c) {
		while (i--) {
			/* Program a new sequence: CCA duration is 8 symbols (128us) */
			MCR20Drv_DirectAccessSPIWrite(PHY_CTRL1, phyReg | gCCA_c);
			/* Wait for sequence to finish */
			while (!(MCR20Drv_DirectAccessSPIRead(IRQSTS1) & cIRQSTS1_SEQIRQ))
				;
			/* Set XCVR to Idle */
			phyReg &= ~(cPHY_CTRL1_XCVSEQ);
			MCR20Drv_DirectAccessSPIWrite(PHY_CTRL1, phyReg);
			/* Read new 8 bit random number */
			*ptr++ = MCR20Drv_IndirectAccessSPIRead(_RNG);
			/* Clear interrupt flag */
			MCR20Drv_DirectAccessSPIWrite(IRQSTS1, cIRQSTS1_SEQIRQ);
		}
	} else {
		*pRandomNo = MCR20Drv_IndirectAccessSPIRead(_RNG);
	}

	MCR20Drv_IRQ_Enable();
}

/*! *********************************************************************************
 * \brief  Set the state of the FP bit of an outgoing ACK frame
 *
 * \param[in]  FP  the state of the FramePending bit
 *
 ********************************************************************************** */
void MCR20A_SetFpManually(bool_t FP) {
	uint8_t phyReg;

	/* Disable the Source Address Matching feature and set FP manually */
#if gPhyUseReducedSpiAccess_d
	phyReg = mStatusAndControlRegs[SRC_CTRL] & ~(cSRC_CTRL_SRCADDR_EN);
#else
	phyReg = MCR20Drv_DirectAccessSPIRead(SRC_CTRL) & ~(cSRC_CTRL_SRCADDR_EN);
#endif

	if (FP)
		phyReg |= cSRC_CTRL_ACK_FRM_PND;
	else
		phyReg &= ~(cSRC_CTRL_ACK_FRM_PND);

	MCR20Drv_DirectAccessSPIWrite(SRC_CTRL, phyReg);
#if gPhyUseReducedSpiAccess_d
	mStatusAndControlRegs[SRC_CTRL] = phyReg;
#endif
}

uint8_t MCR20A_SetANTPadStateRequest(bool_t antAB_on, bool_t rxtxSwitch_on) {
	uint8_t phyReg;

	phyReg = MCR20Drv_IndirectAccessSPIRead(ANT_PAD_CTRL);
	antAB_on ? (phyReg |= 0x02) : (phyReg &= ~0x02);
	rxtxSwitch_on ? (phyReg |= 0x01) : (phyReg &= ~0x01);
	MCR20Drv_IndirectAccessSPIWrite(ANT_PAD_CTRL, phyReg);

	return gPhySuccess_c;
}

uint8_t MCR20A_SetANTPadStrengthRequest(bool_t hiStrength) {
	uint8_t phyReg;

	phyReg = MCR20Drv_IndirectAccessSPIRead(MISC_PAD_CTRL);
	hiStrength ? (phyReg |= cMISC_PAD_CTRL_ANTX_CURR) : (phyReg &=
							~cMISC_PAD_CTRL_ANTX_CURR);
	MCR20Drv_IndirectAccessSPIWrite(MISC_PAD_CTRL, phyReg);

	return gPhySuccess_c;
}

uint8_t MCR20A_SetANTPadInvertedRequest(bool_t invAntA, bool_t invAntB,
		bool_t invTx, bool_t invRx) {
	uint8_t phyReg;

	phyReg = MCR20Drv_IndirectAccessSPIRead(MISC_PAD_CTRL);
	invAntA ? (phyReg |= 0x10) : (phyReg &= ~0x10);
	invAntB ? (phyReg |= 0x20) : (phyReg &= ~0x20);
	invTx ? (phyReg |= 0x40) : (phyReg &= ~0x40);
	invRx ? (phyReg |= 0x80) : (phyReg &= ~0x80);
	MCR20Drv_IndirectAccessSPIWrite(MISC_PAD_CTRL, phyReg);

	return gPhySuccess_c;
}

/*! *********************************************************************************
 * \brief  This function compute the hash code for an 802.15.4 device
 *
 * \param[in]  pAddr     Pointer to an 802.15.4 address
 * \param[in]  addrMode  The 802.15.4 addressing mode
 * \param[in]  PanId     The 802.15.2 PAN Id
 *
 * \return  hash code
 *
 ********************************************************************************** */
uint16_t MCR20A_GetChecksum(uint8_t *pAddr, uint8_t addrMode, uint16_t PanId) {
	uint16_t checksum;

	/* Short address */
	checksum = PanId;
	checksum += *pAddr++;
	checksum += (*pAddr++) << 8;

	if (addrMode == 3) {
		/* Extended address */
		checksum += *pAddr++;
		checksum += (*pAddr++) << 8;
		checksum += *pAddr++;
		checksum += (*pAddr++) << 8;
		checksum += *pAddr++;
		checksum += (*pAddr++) << 8;
	}

	return checksum;
}

/*! *********************************************************************************
 * \brief  This function adds an 802.15.4 device to the neighbor table.
 *         If a polling device is not in the neighbor table, the ACK will have FP=1
 *
 * \param[in]  pAddr     Pointer to an 802.15.4 address
 * \param[in]  addrMode  The 802.15.4 addressing mode
 * \param[in]  PanId     The 802.15.2 PAN Id
 *
 ********************************************************************************** */
uint8_t MCR20A_AddToNeighborTable(uint8_t *pAddr, uint8_t addrMode,
		uint16_t PanId) {
#if gPhyNeighborTableSize_d
	uint16_t checksum = PhyGetChecksum(pAddr, addrMode, PanId);

	if( PhyCheckNeighborTable(checksum) ) {
		/* Device is allready in the table */
		return 0;
	}

	if( mPhyNeighbotTableUsage < gPhyNeighborTableSize_d ) {
		mPhyNeighborTable[mPhyNeighbotTableUsage++] = checksum;
		return 0;
	}
#endif
	return 1;
}

/*! *********************************************************************************
 * \brief  This function removes an 802.15.4 device to the neighbor table.
 *         If a polling device is not in the neighbor table, the ACK will have FP=1
 *
 * \param[in]  pAddr     Pointer to an 802.15.4 address
 * \param[in]  addrMode  The 802.15.4 addressing mode
 * \param[in]  PanId     The 802.15.2 PAN Id
 *
 ********************************************************************************** */
uint8_t MCR20A_RemoveFromNeighborTable(uint8_t *pAddr, uint8_t addrMode,
		uint16_t PanId) {
#if gPhyNeighborTableSize_d
	uint16_t i, checksum = PhyGetChecksum(pAddr, addrMode, PanId);

	for( i = 0; i < mPhyNeighbotTableUsage; i++ ) {
		if( checksum == mPhyNeighborTable[i] ) {
			mPhyNeighborTable[i] = mPhyNeighborTable[--mPhyNeighbotTableUsage];
			mPhyNeighborTable[mPhyNeighbotTableUsage] = 0;
			return 0;
		}
	}
#endif
	return 1;
}

/*! *********************************************************************************
 * \brief  This function checks if an 802.15.4 device is in the neighbor table.
 *         If a polling device is not in the neighbor table, the ACK will have FP=1
 *
 * \param[in]  pAddr     Pointer to an 802.15.4 address
 * \param[in]  addrMode  The 802.15.4 addressing mode
 * \param[in]  PanId     The 802.15.2 PAN Id
 *
 * \return  TRUE if the device is present in the neighbor table, FALSE if not.
 *
 ********************************************************************************** */
bool_t MCR20A_CheckNeighborTable(uint16_t checksum) {
#if gPhyNeighborTableSize_d
	uint16_t i;

	for( i = 0; i < mPhyNeighbotTableUsage; i++ ) {
		if( checksum == mPhyNeighborTable[i] ) {
			return TRUE;
		}
	}
#endif
	return FALSE;
}

uint32_t MCR20A_GetMACTimer(void) {
	uint32_t rval;
	//从射频模块获取硬件绝对时间
	//读取时间为3个字节
	MCR20Drv_DirectAccessSPIMultiByteRead((uint8_t) EVENT_TMR_LSB,
			(uint8_t *) &rval, 3);
	return rval;
}

void MCR20A_GetIEEEAddr(uint8_t *pLongAddr, uint8_t pan) {
	if (NULL == pLongAddr)
		return;
	if (0 == pan)
		MCR20Drv_IndirectAccessSPIMultiByteRead((uint8_t) MACLONGADDRS0_0,
				pLongAddr, 8);
	else
		MCR20Drv_IndirectAccessSPIMultiByteRead((uint8_t) MACLONGADDRS1_0,
				pLongAddr, 8);

}

/* 2405   2410    2415    2420    2425    2430    2435    2440    2445    2450    2455    2460    2465    2470    2475    2480 */
static const uint8_t pll_int[16] = { 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0C,
		0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D };
static const uint16_t pll_frac[16] = { 0x2800, 0x5000, 0x7800, 0xA000, 0xC800,
		0xF000, 0x1800, 0x4000, 0x6800, 0x9000, 0xB800, 0xE000, 0x0800, 0x3000,
		0x5800, 0x8000 };

phyStatus_t MCR20A_SetCurrentChannelRequest(uint8_t channel, uint8_t pan) {
#ifdef PHY_PARAMETERS_VALIDATION
	if((channel < 11) || (channel > 26)) {
		return gPhyInvalidParameter_c;
	}
#endif /* PHY_PARAMETERS_VALIDATION */

	if (!pan) {
		MCR20Drv_DirectAccessSPIWrite(PLL_INT0, pll_int[channel - 11]);
		MCR20Drv_DirectAccessSPIMultiByteWrite(PLL_FRAC0_LSB,
				(uint8_t *) &pll_frac[channel - 11], 2);
	} else {
		MCR20Drv_IndirectAccessSPIWrite(PLL_INT1, pll_int[channel - 11]);
		MCR20Drv_IndirectAccessSPIMultiByteWrite(PLL_FRAC1_LSB,
				(uint8_t *) &pll_frac[channel - 11], 2);
	}

	return gPhySuccess_c;
}

phyStatus_t MCR20A_SetPwrLevelRequest(uint8_t pwrStep) {
#ifdef PHY_PARAMETERS_VALIDATION
	if((pwrStep < 8) || (pwrStep > 28)) { /* -30 dBm to 10 dBm */
		return gPhyInvalidParameter_c;
	}
#endif /* PHY_PARAMETERS_VALIDATION */

	/* Do not exceed the Tx power limit for the current channel ,最大28*/
	if (pwrStep > 28) {
		pwrStep = 28;
	} else if (pwrStep < 8) {
		pwrStep = 8;
	}

	MCR20Drv_DirectAccessSPIWrite(PA_PWR, pwrStep);

	return gPhySuccess_c;
}

phyStatus_t MCR20A_RemoveFromIndirect(uint8_t index) {
	uint8_t srcCtrlReg;

	if (index >= gPhyIndirectQueueSize_c)
		return gPhyInvalidParameter_c;

	srcCtrlReg = (uint8_t)(
			((index & cSRC_CTRL_INDEX) << cSRC_CTRL_INDEX_Shift_c)
					| (cSRC_CTRL_SRCADDR_EN) | (cSRC_CTRL_INDEX_DISABLE));

	MCR20Drv_DirectAccessSPIWrite((uint8_t) SRC_CTRL, srcCtrlReg);

	return gPhySuccess_c;
}

void MCR20A_IsrSeqCleanup(void) {
	mStatusAndControlRegs[PHY_CTRL1_INDEX_c] &= ~(cPHY_CTRL1_XCVSEQ);
	mStatusAndControlRegs[PHY_CTRL1_INDEX_c] &= ~(cPHY_CTRL1_TMRTRIGEN); /* disable autosequence start by TC2 match */
	mStatusAndControlRegs[PHY_CTRL2_INDEX_c] |= (cPHY_CTRL2_CCAMSK
			| cPHY_CTRL2_RXMSK | cPHY_CTRL2_TXMSK | cPHY_CTRL2_SEQMSK);

#if gPhyUseReducedSpiAccess_d
	mStatusAndControlRegs[PHY_IRQSTS3_INDEX_c] |= cIRQSTS3_TMR3MSK; /* mask TMR3 interrupt */
#else
	mStatusAndControlRegs[PHY_IRQSTS3_INDEX_c] &= 0xF0;
	mStatusAndControlRegs[PHY_IRQSTS3_INDEX_c] |= cIRQSTS3_TMR3MSK; /* mask TMR3 interrupt */

	/* Clear transceiver interrupts, mask SEQ, RX, TX and CCA interrupts
	 and set the PHY sequencer back to IDLE */
	MCR20Drv_DirectAccessSPIMultiByteWrite(IRQSTS1, mStatusAndControlRegs, 5);
#endif
}

uint8_t MCR20A_LqiConvert(uint8_t hwLqi) {
	uint32_t tmpLQI;

	/* LQI Saturation Level */
	if (hwLqi >= 230) {
		return 0xFF;
	} else if (hwLqi <= 9) {
		return 0;
	} else {
		/* Rescale the LQI values from min to saturation to the 0x00 - 0xFF range */
		/* The LQI value mst be multiplied by ~1.1087 */
		/* tmpLQI =  hwLqi * 7123 ~= hwLqi * 65536 * 0.1087 = hwLqi * 2^16 * 0.1087*/
		tmpLQI = ((uint32_t) hwLqi * (uint32_t) 7123);
		/* tmpLQI =  (tmpLQI / 2^16) + hwLqi */
		tmpLQI = (uint32_t) (tmpLQI >> 16) + (uint32_t) hwLqi;

		return (uint8_t) tmpLQI;
	}
}

enum {
	REG_TYPE_KEY = 0,
	REG_TYPE_DATA,
	REG_TYPE_CTR,
	REG_TYPE_CTR_RESULT,
	REG_TYPE_CBC_RESULT,
	REG_TYPE_MAC,
	REG_TYPE_AES_RESULT,
};

//AES128加密,返回值 1--成功，0--失败
//MCR20A硬件没有AES128解密功能
uint8_t MCR20A_AES128_Encrypt(uint8_t key[16], uint8_t in[16], uint8_t out[16]) {
	uint8_t phyReg;
	uint8_t asm_ctrl[2];
	uint8_t buf[16];

	//1.将ASM_CTRL2置为KEY
	asm_ctrl[1] = REG_TYPE_KEY << cASM_CTRL2_DATA_REG_TYPE_SEL_Shift_c; //key
	MCR20Drv_DirectAccessSPIWrite(ASM_CTRL2, asm_ctrl[1]);
	//2.写入KEY数据
	MCR20Drv_DirectAccessSPIMultiByteWrite(ASM_DATA_0, key, 16);

	//3.将ASM_CTRL1置为CBC加密模式   CTR=0，CBC=1，AES=0
	asm_ctrl[0] = cASM_CTRL1_AES;
	MCR20Drv_DirectAccessSPIWrite(ASM_CTRL1, asm_ctrl[0]);
	//4.将ASM_CTRL2置为TEXT数据
	asm_ctrl[1] = REG_TYPE_DATA << cASM_CTRL2_DATA_REG_TYPE_SEL_Shift_c; //text
	MCR20Drv_DirectAccessSPIWrite(ASM_CTRL2, asm_ctrl[1]);
	//5.写入TEXT数据
	MCR20Drv_DirectAccessSPIMultiByteWrite(ASM_DATA_0, in, 16);
	//6.将ASM_CTRL1置为START=1
	asm_ctrl[0] |= cASM_CTRL1_START;
	MCR20Drv_DirectAccessSPIWrite(ASM_CTRL1, asm_ctrl[0]);
	//7.等待ASM_IRQ
	for (int i = 0; i < 100; i++)
		;

	mStatusAndControlRegs[0] = MCR20Drv_DirectAccessSPIMultiByteRead(IRQSTS2,
			&mStatusAndControlRegs[1], 2);
	phyReg = mStatusAndControlRegs[1] & cIRQSTS2_ASM_IRQ;
	if (phyReg) {
		//清中断标志
		MCR20Drv_DirectAccessSPIMultiByteWrite(IRQSTS1, mStatusAndControlRegs,
				3);
		//8.将ASM_CTRL2置为AES结果
		asm_ctrl[1] = REG_TYPE_AES_RESULT
				<< cASM_CTRL2_DATA_REG_TYPE_SEL_Shift_c;
		MCR20Drv_DirectAccessSPIWrite(ASM_CTRL2, asm_ctrl[1]);
		//9.读取数据
		MCR20Drv_DirectAccessSPIMultiByteRead(ASM_DATA_0, out, 16);

		return 1; //加密成功

		//加密结束，可以读取数据
		//printf("OK,耗时 %d us\n", us_now - us_pre);
	}

	return 0; //加密失败
}
//清除MCR20A硬件加密相关寄存器的值
void MCR20A_ASM_Clear() {
	MCR20Drv_DirectAccessSPIWrite(ASM_CTRL1, cASM_CTRL1_CLEAR);
}
//CBC重新装载IV，硬件自动将上次加密结果作为下次加密向量进行装载
void MCR20A_CBC128_Reload() {
	MCR20Drv_DirectAccessSPIWrite(ASM_CTRL1, cASM_CTRL1_LOAD_MAC);
}
//硬件加密自测试，在启动加密之前必须先调用该函数一次，确保硬件正常！
//返回值，1--成功，0--失败
uint8_t MCR20A_ASM_Selftest() {
	uint8_t phyReg;
	//1.开启ASM_CLK_EN,关闭AUTODOZE
	phyReg = MCR20Drv_DirectAccessSPIRead((uint8_t) PWR_MODES);
	phyReg |= cPWR_MODES_ASM_CLK_EN;
	phyReg &= ~cPWR_MODES_AUTODOZE;
	MCR20Drv_DirectAccessSPIWrite(PWR_MODES, phyReg);
	//2.开启SELFTST和START
	MCR20Drv_DirectAccessSPIWrite(ASM_CTRL1,
			cASM_CTRL1_SELFTST | cASM_CTRL1_START);
	//3.等待至少3330MCR20A_clock=3330/32MHz=104us

	//STM32F207 CLOCK=1/120MHz=0.008us ，因此 104us/(1/120MHz)=12480 STM32_clock
	//这里延迟200*100 STM32_CLOCK保证超过104us
	for (int i = 0; i < 200; i++)
		for (int j = 0; j < 100; j++)
			;

	mStatusAndControlRegs[0] = MCR20Drv_DirectAccessSPIMultiByteRead(IRQSTS2,
			&mStatusAndControlRegs[1], 7);

	//读取结果
	phyReg = MCR20Drv_DirectAccessSPIRead(ASM_CTRL2);
	if (phyReg & cASM_CTRL2_TSTPAS) {
		//测试通过
		return 1;
	}
	return 0; //测试未通过
}

//CTR128加密,返回值 1--成功，0--失败
uint8_t MCR20A_CTR128_Encrypt(uint8_t key[16], uint8_t in[16], uint8_t cnt[16],
		uint8_t out[16]) {
	uint8_t phyReg;
	uint8_t asm_ctrl[2];
	uint8_t buf[16];

	//1.将ASM_CTRL2置为KEY
	asm_ctrl[1] = REG_TYPE_KEY << cASM_CTRL2_DATA_REG_TYPE_SEL_Shift_c; //key
	MCR20Drv_DirectAccessSPIWrite(ASM_CTRL2, asm_ctrl[1]);
	//2.写入KEY数据
	MCR20Drv_DirectAccessSPIMultiByteWrite(ASM_DATA_0, key, 16);

	//3.将ASM_CTRL1置为CBC加密模式   CTR=1，CBC=0，AES=0
	asm_ctrl[0] = cASM_CTRL1_CTR;
	MCR20Drv_DirectAccessSPIWrite(ASM_CTRL1, asm_ctrl[0]);
	//4.将ASM_CTRL2置为TEXT数据
	asm_ctrl[1] = REG_TYPE_DATA << cASM_CTRL2_DATA_REG_TYPE_SEL_Shift_c; //text
	MCR20Drv_DirectAccessSPIWrite(ASM_CTRL2, asm_ctrl[1]);
	//5.写入TEXT数据
	MCR20Drv_DirectAccessSPIMultiByteWrite(ASM_DATA_0, in, 16);

	//6.将ASM_CTRL2置为CTR数据
	asm_ctrl[1] = REG_TYPE_CTR << cASM_CTRL2_DATA_REG_TYPE_SEL_Shift_c; //text
	MCR20Drv_DirectAccessSPIWrite(ASM_CTRL2, asm_ctrl[1]);
	//7.写入CTR数据
	MCR20Drv_DirectAccessSPIMultiByteWrite(ASM_DATA_0, cnt, 16);

	//8.将ASM_CTRL1置为START=1
	asm_ctrl[0] |= cASM_CTRL1_START;
	MCR20Drv_DirectAccessSPIWrite(ASM_CTRL1, asm_ctrl[0]);
	//9.等待ASM_IRQ,只需要等待11个MCR20A_clock=11*(1/32MHz)=0.344us
	for (int i = 0; i < 50; i++)
		;

	mStatusAndControlRegs[0] = MCR20Drv_DirectAccessSPIMultiByteRead(IRQSTS2,
			&mStatusAndControlRegs[1], 2);
	phyReg = mStatusAndControlRegs[1] & cIRQSTS2_ASM_IRQ;
	if (phyReg) {
		//清中断标志
		MCR20Drv_DirectAccessSPIMultiByteWrite(IRQSTS1, mStatusAndControlRegs,
				3);
		//10.将ASM_CTRL2置为AES结果
		asm_ctrl[1] = REG_TYPE_CTR_RESULT
				<< cASM_CTRL2_DATA_REG_TYPE_SEL_Shift_c;
		MCR20Drv_DirectAccessSPIWrite(ASM_CTRL2, asm_ctrl[1]);
		//11.读取数据
		MCR20Drv_DirectAccessSPIMultiByteRead(ASM_DATA_0, out, 16);

		return 1; //加密成功

		//加密结束，可以读取数据
		//printf("OK,耗时 %d us\n", us_now - us_pre);
	}

	return 0; //加密失败
}
