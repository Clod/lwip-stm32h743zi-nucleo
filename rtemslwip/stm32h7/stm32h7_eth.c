/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : ethernetif.c
  * Description        : This file provides code for the configuration
  *                      of the ethernetif.c MiddleWare.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include <bsp.h>
#include <stm32h7xx_hal.h>
#include "lwip/opt.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "netif/etharp.h"
#include "lwip/ethip6.h"
#include "stm32h7_eth.h"
#include "stm32h7_lan8742.h"
#include <string.h>
#include <stddef.h>
#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include <rtems/irq-extension.h>

/* Within 'USER CODE' section, code will be kept by default at each generation */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* Private define ------------------------------------------------------------*/
/* The time to block waiting for input. */
#define TIME_WAITING_FOR_INPUT ( 0 )
/* USER CODE BEGIN OS_THREAD_STACK_SIZE_WITH_RTOS */
/* ETH_CODE: increase stack size, otherwise there
 * might be overflow in more advanced applications.
 * Lower optimization can increase the stack usage
 * and cause stack overflows in some cases.
 */
/* Stack size of the interface thread */
#define INTERFACE_THREAD_STACK_SIZE ( 1024 )
/* USER CODE END OS_THREAD_STACK_SIZE_WITH_RTOS */
/* Network interface name */
#define IFNAME0 's'
#define IFNAME1 't'

/* ETH Setting  */
#define ETH_DMA_TRANSMIT_TIMEOUT               ( 20U )
#define ETH_TX_BUFFER_MAX             ((ETH_TX_DESC_CNT) * 2U)
/* ETH_RX_BUFFER_SIZE parameter is defined in lwipopts.h */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/* Private variables ---------------------------------------------------------*/
/*
@Note: This interface is implemented to operate in zero-copy mode only:
        - Rx buffers are allocated statically and passed directly to the LwIP stack
          they will return back to ETH DMA after been processed by the stack.
        - Tx Buffers will be allocated from LwIP stack memory heap,
          then passed to ETH HAL driver.

@Notes:
  1.a. ETH DMA Rx descriptors must be contiguous, the default count is 4,
       to customize it please redefine ETH_RX_DESC_CNT in ETH GUI (Rx Descriptor Length)
       so that updated value will be generated in stm32xxxx_hal_conf.h
  1.b. ETH DMA Tx descriptors must be contiguous, the default count is 4,
       to customize it please redefine ETH_TX_DESC_CNT in ETH GUI (Tx Descriptor Length)
       so that updated value will be generated in stm32xxxx_hal_conf.h

  2.a. Rx Buffers number must be between ETH_RX_DESC_CNT and 2*ETH_RX_DESC_CNT
  2.b. Rx Buffers must have the same size: ETH_RX_BUFFER_SIZE, this value must
       passed to ETH DMA in the init field (heth.Init.RxBuffLen)
  2.c  The RX Ruffers addresses and sizes must be properly defined to be aligned
       to L1-CACHE line size (32 bytes).
*/

/* Data Type Definitions */
typedef enum
{
  RX_ALLOC_OK       = 0x00,
  RX_ALLOC_ERROR    = 0x01
} RxAllocStatusTypeDef;

/* ETH_CODE: Ensure RxBuff_t is aligned to 32 bytes for DMA compatibility.
 * The struct includes pbuf_custom (16 bytes on ARM) + 32-byte aligned buffer.
 * Total size must be multiple of 32 for proper memory pool alignment. */
typedef struct __attribute__((aligned(32)))
{
  struct pbuf_custom pbuf_custom;
  uint8_t buff[ETH_RX_BUFFER_SIZE];
} RxBuff_t;

/* Memory Pool Manual Declaration (points to D2 SRAM) */
#define ETH_RX_BUFFER_CNT             32U
#define RX_POOL_BASE_ADDR             0x30000600

/* ETH_CODE: Define TX Bounce Buffer in D2 SRAM to ensure DMA accessibility */
#define ETH_TX_BUFFER_ADDR            0x3000D000
#define ETH_TX_BUFFER_MAX_SIZE        1536

#if MEMP_STATS
static struct stats_mem memp_stats_RX_POOL;
#endif
static struct memp *memp_tab_RX_POOL;

const struct memp_desc memp_RX_POOL = {
  DECLARE_LWIP_MEMPOOL_DESC("Zero-copy RX PBUF pool")
#if MEMP_STATS
  &memp_stats_RX_POOL,
#endif
  LWIP_MEM_ALIGN_SIZE(sizeof(RxBuff_t)),
  ETH_RX_BUFFER_CNT,
  (uint8_t *)RX_POOL_BASE_ADDR,
  &memp_tab_RX_POOL
};

/* Variable Definitions */
static uint8_t RxAllocStatus;

/* ETH_CODE: The STM32H7 DMA expects exactly 16 bytes (4 words) per descriptor.
 * We use a packed structure to match hardware requirements. */
typedef struct __attribute__((packed))
{
  __IO uint32_t DESC0;
  __IO uint32_t DESC1;
  __IO uint32_t DESC2;
  __IO uint32_t DESC3;
} ETH_DMADescTypeDef_Shadow;

/* Separate array to store backup buffer addresses (DMA overwrites DESC0 on completion) */
static uint32_t DMARxDscrBackup[ETH_RX_DESC_CNT];
static uint32_t DMATxDscrBackup[ETH_TX_DESC_CNT] __attribute__((unused));

__IO uint32_t TxPkt = 0;
__IO uint32_t RxPkt = 0;

ETH_DMADescTypeDef_Shadow *DMARxDscrTab = (ETH_DMADescTypeDef_Shadow *)0x30000000; /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef_Shadow *DMATxDscrTab = (ETH_DMADescTypeDef_Shadow *)0x30000400; /* Ethernet Tx DMA Descriptors (move away to avoid overlap) */

__IO uint32_t EthIrqCount = 0;
__IO uint32_t RxIrqCount = 0;

/* USER CODE END 2 */

sys_sem_t RxPktSemaphore;   /* Semaphore to signal incoming packets */
sys_sem_t TxPktSemaphore;   /* Semaphore to signal transmit packet complete */

/* Global Ethernet handle */
ETH_HandleTypeDef heth;
ETH_TxPacketConfig TxConfig;

/* Private function prototypes -----------------------------------------------*/
int32_t ETH_PHY_IO_Init(void);
int32_t ETH_PHY_IO_DeInit (void);
int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal);
int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal);
int32_t ETH_PHY_IO_GetTick(void);
static void MPU_Config(void);

lan8742_Object_t LAN8742;
lan8742_IOCtx_t  LAN8742_IOCtx = {ETH_PHY_IO_Init,
                                  ETH_PHY_IO_DeInit,
                                  ETH_PHY_IO_WriteReg,
                                  ETH_PHY_IO_ReadReg,
                                  ETH_PHY_IO_GetTick};

/* USER CODE BEGIN 3 */

/* USER CODE END 3 */

/* Private functions ---------------------------------------------------------*/
void pbuf_free_custom(struct pbuf *p);
static void ethernetif_rebuild_rx_descriptors(void);
static void stm32h7_recycle_rx_descriptor(uint32_t idx);

/* Helper to kick the RX DMA tail pointer correctly for a ring */
static void stm32h7_eth_kick_rx_dma(void)
{
    // printf("\n========== KICK_RX_DMA ==========\n");
    // printf("KICK: Setting DMACRDTPR to 0x%08lx (last desc)\n",
    //        (unsigned long)&DMARxDscrTab[ETH_RX_DESC_CNT - 1]);
    
    /* ETH_CODE: FIX - The tail pointer must point AFTER the last descriptor.
     * In Ring mode, the DMA processes descriptors up to (but not including)
     * the address in the Tail Pointer. Pointing to the last descriptor (N-1)
     * causes the DMA to stop before processing it.
     */
    heth.Instance->DMACRDTPR = (uint32_t)(DMARxDscrTab + ETH_RX_DESC_CNT);
    __DSB();
    
    /* Write Poll Demand to force immediate re-fetch of descriptors */
    *(__IO uint32_t *)((uint32_t)heth.Instance + 0x104C) = 0;
    
    // printf("KICK: Poll demand written, DMACRDTPR now 0x%08lx\n",
    //        (unsigned long)heth.Instance->DMACRDTPR);
    // printf("KICK: RxDescIdx=%lu, RxBuildDescIdx=%lu, RxBuildDescCnt=%lu\n",
    //        (unsigned long)heth.RxDescList.RxDescIdx,
    //        (unsigned long)heth.RxDescList.RxBuildDescIdx,
    //        (unsigned long)heth.RxDescList.RxBuildDescCnt);
    // printf("========== KICK_RX_DMA END ==========\n\n");
}

static void stm32h7_eth_interrupt_handler(void *arg)
{
  EthIrqCount++;
  // printf("\n========== ETH IRQ START ==========\n");
  // printf("ETH IRQ: EthIrqCount=%lu\n", (unsigned long)EthIrqCount);
  
  /* Dump DMA status registers BEFORE handling */
  // printf("ETH IRQ: DMACSR=0x%08lx, DMACIER=0x%08lx\n",
  //        (unsigned long)heth.Instance->DMACSR, (unsigned long)heth.Instance->DMACIER);
  // printf("ETH IRQ: MACISR=0x%08lx, MACIER=0x%08lx\n",
  //        (unsigned long)heth.Instance->MACISR, (unsigned long)heth.Instance->MACIER);
  
  /* Dump descriptor indices */
  // printf("ETH IRQ: RxDescIdx=%lu, RxBuildDescIdx=%lu, RxBuildDescCnt=%lu\n",
  //        (unsigned long)heth.RxDescList.RxDescIdx,
  //        (unsigned long)heth.RxDescList.RxBuildDescIdx,
  //        (unsigned long)heth.RxDescList.RxBuildDescCnt);
  
  /* Dump all RX descriptors */
  // printf("ETH IRQ: RX Descriptors state:\n");
  // for(int i=0; i<ETH_RX_DESC_CNT; i++) {
  //   ETH_DMADescTypeDef_Shadow *d = &DMARxDscrTab[i];
  //   printf("  Desc[%d]: DESC0=0x%08lx, DESC1=0x%08lx, DESC2=0x%08lx, DESC3=0x%08lx\n", i,
  //          (unsigned long)d->DESC0, (unsigned long)d->DESC1,
  //          (unsigned long)d->DESC2, (unsigned long)d->DESC3);
  // }
  
  /* Dump DMA tail pointer */
  // printf("ETH IRQ: DMACRDTPR=0x%08lx (should be 0x%08lx)\n",
  //        (unsigned long)heth.Instance->DMACRDTPR,
  //        (unsigned long)&DMARxDscrTab[ETH_RX_DESC_CNT - 1]);
  
  HAL_ETH_IRQHandler(&heth);
   
  /* ETH_CODE: After HAL processing, check if we need to kick the DMA again.
   * If descriptors were recycled by the error callback, kick DMA to resume. */
  uint32_t dmacsr = heth.Instance->DMACSR;
  // printf("ETH IRQ: After HAL - DMACSR=0x%08lx\n", (unsigned long)dmacsr);
  
  if (dmacsr & ETH_DMACSR_RBU) {
      /* RBU bit is sticky. Clear it manually to allow the DMA to resume. */
      printf("ETH IRQ: RBU detected! Clearing and resuming DMA...\n");
      heth.Instance->DMACSR = (ETH_DMACSR_RBU | ETH_DMACSR_AIS);
      stm32h7_eth_kick_rx_dma();
      printf("ETH IRQ: RBU Cleared & DMA Resumed\n");
  }
  
  /* Signal RX semaphore to process any received packets */
  // printf("ETH IRQ: Signaling RxPktSemaphore\n");
  sys_sem_signal(&RxPktSemaphore);
  // printf("========== ETH IRQ END ==========\n\n");
}

/**
  * @brief  Ethernet Rx Transfer completed callback
  * @param  handlerEth: ETH handler
  * @retval None
  */
void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *handlerEth)
{
  RxIrqCount++;
  // printf("\n========== ETH RX COMPLETE CALLBACK ==========\n");
  // printf("ETH RX Callback: RxIrqCount=%lu\n", (unsigned long)RxIrqCount);
  // printf("ETH RX Callback: RxDescIdx=%lu, RxBuildDescIdx=%lu, RxBuildDescCnt=%lu\n",
  //        (unsigned long)handlerEth->RxDescList.RxDescIdx,
  //        (unsigned long)handlerEth->RxDescList.RxBuildDescIdx,
  //        (unsigned long)handlerEth->RxDescList.RxBuildDescCnt);
  
  /* Dump descriptor at current index */
  // uint32_t idx = handlerEth->RxDescList.RxDescIdx;
  // ETH_DMADescTypeDef_Shadow *d = &DMARxDscrTab[idx];
  // printf("ETH RX Callback: Desc[%lu] DESC0=0x%08lx, DESC1=0x%08lx, DESC2=0x%08lx, DESC3=0x%08lx\n",
  //        (unsigned long)idx, (unsigned long)d->DESC0, (unsigned long)d->DESC1,
  //        (unsigned long)d->DESC2, (unsigned long)d->DESC3);
  
  sys_sem_signal(&RxPktSemaphore);
  // printf("========== ETH RX COMPLETE CALLBACK END ==========\n\n");
}
/**
  * @brief  Ethernet Tx Transfer completed callback
  * @param  handlerEth: ETH handler
  * @retval None
  */
void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *handlerEth)
{
  // printf("\n========== ETH TX COMPLETE CALLBACK ==========\n");
  // printf("ETH TX Callback: Tx complete\n");
  // printf("ETH TX Callback: CurTxDesc=%lu\n", (unsigned long)handlerEth->TxDescList.CurTxDesc);
  sys_sem_signal(&TxPktSemaphore);
  // printf("========== ETH TX COMPLETE CALLBACK END ==========\n\n");
}
/**
  * @brief  Ethernet DMA transfer error callback
  * @param  handlerEth: ETH handler
  * @retval None
  */
void HAL_ETH_ErrorCallback(ETH_HandleTypeDef *handlerEth)
{
  uint32_t dma_err = HAL_ETH_GetDMAError(handlerEth);
  printf("\n========== ETH ERROR CALLBACK ==========\n");
  printf("ETH Error Callback: DMA Error=0x%08lx\n", (unsigned long)dma_err);
  
  /* Decode error bits */
  if (dma_err & ETH_DMACSR_RBU) printf("  - RBU: Receive Buffer Unavailable\n");
  if (dma_err & ETH_DMACSR_RPS) printf("  - RPS: Receive Process Stopped\n");
  if (dma_err & ETH_DMACSR_RWT) printf("  - RWT: Receive Watchdog Timeout\n");
  if (dma_err & ETH_DMACSR_ETI) printf("  - ETI: Early Transmit Interrupt\n");
  if (dma_err & ETH_DMACSR_FBE) printf("  - FBE: Fatal Bus Error\n");
  if (dma_err & ETH_DMACSR_ERI) printf("  - ERI: Early Receive Interrupt\n");
  if (dma_err & ETH_DMACSR_AIS) printf("  - AIS: Abnormal Interrupt Summary\n");
  if (dma_err & ETH_DMACSR_NIS) printf("  - NIS: Normal Interrupt Summary\n");
  
  /* Diagnostic: Dump descriptor and HAL state on error */
  printf("ETH Error: RxIdx=%lu, RxBuildIdx=%lu, RxBuildCnt=%lu\n",
         (unsigned long)handlerEth->RxDescList.RxDescIdx,
         (unsigned long)handlerEth->RxDescList.RxBuildDescIdx,
         (unsigned long)handlerEth->RxDescList.RxBuildDescCnt);
  
  printf("ETH Error: DMACSR=0x%08lx, DMACRDTPR=0x%08lx\n",
         (unsigned long)handlerEth->Instance->DMACSR,
         (unsigned long)handlerEth->Instance->DMACRDTPR);
  
  for(int i=0; i<ETH_RX_DESC_CNT; i++) {
    ETH_DMADescTypeDef_Shadow *d = &DMARxDscrTab[i];
    printf("RX Desc %d [0x%08lx]: 0x%08lx 0x%08lx 0x%08lx 0x%08lx\n", i,
           (unsigned long)d,
           (unsigned long)d->DESC0,
           (unsigned long)d->DESC1,
           (unsigned long)d->DESC2,
           (unsigned long)d->DESC3);
  }
  printf("========== ETH ERROR CALLBACK END ==========\n\n");

  if((dma_err & ETH_DMACSR_RBU) == ETH_DMACSR_RBU)
  {
     printf("ETH Error: Receive Buffer Unavailable\n");
     
     /* ETH_CODE: Properly handle RBU by recycling the current descriptor.
      * The DMA has written back a descriptor but we need to recycle it. */
     
     /* Check if we have a CPU-owned descriptor to recycle */
     uint32_t idx = handlerEth->RxDescList.RxDescIdx;
     ETH_DMADescTypeDef_Shadow *d = &DMARxDscrTab[idx];
     
     /* Invalidate cache to read actual RAM value */
     SCB_InvalidateDCache_by_Addr((uint32_t *)d, sizeof(ETH_DMADescTypeDef_Shadow));
     
     printf("ETH RBU: Recycle Idx=%lu, DESC3=0x%08lx\n", (unsigned long)idx, (unsigned long)d->DESC3);
     
     /* ETH_CODE: Check if the descriptor is owned by CPU (Bit 31 = 0) */
     if ((d->DESC3 & 0x80000000) == 0) {
        /* Descriptor is CPU-owned. It might contain a valid packet! */
        /* H7 RDES3: Bit 31: OWN, Bit 30: CTXT */
        if ((d->DESC3 & 0x40000000) != 0) {
             /* It is a context descriptor (CTXT=1), safe to recycle. */
              printf("ETH RBU: Context descriptor detected at %lu, recycling...\n", (unsigned long)idx);
        } else {
             /* Normal Packet Descriptor (CTXT=0) with OWN=0. 
              * This contains a VALID packet waiting for the stack. 
              * DO NOT RECYCLE! Just signal the thread to come and get it. */
             printf("ETH RBU: Valid packet at %lu (DESC3=0x%08lx). Deferring to thread.\n", 
                    (unsigned long)idx, (unsigned long)d->DESC3);
             
             /* Clear RBU flag to move on. */
             handlerEth->Instance->DMACSR = (ETH_DMACSR_RBU | ETH_DMACSR_AIS);
             sys_sem_signal(&RxPktSemaphore);
             return;
        }
     } else {
        /* OWN=1: Descriptor is ready for DMA. The hardware just needs a kick. 
         * This can happen if the hardware reached the tail pointer. */
        printf("ETH RBU: Descriptor %lu is DMA owned (ready). Kicking DMA.\n", (unsigned long)idx);
     }
     
     /* Clear flags */
     handlerEth->Instance->DMACSR = (ETH_DMACSR_RBU | ETH_DMACSR_AIS);
     
     /* Kick the DMA using dynamic tail pointer */
     stm32h7_eth_kick_rx_dma();
     
     printf("ETH RBU: DMA kicked, ReadIdx=%lu\n", (unsigned long)idx);
     
     /* Signal the input task to check if it missed anything */
     sys_sem_signal(&RxPktSemaphore);
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/*******************************************************************************
                       LL Driver Interface ( LwIP stack --> ETH)
*******************************************************************************/
/**
 * @brief In this function, the hardware should be initialized.
 * Called from ethernetif_init().
 *
 * @param netif the already initialized lwip network interface structure
 *        for this ethernetif
 */
static void low_level_init(struct netif *netif)
{
  HAL_StatusTypeDef hal_eth_init_status = HAL_OK;

//  uint32_t duplex, speed = 0;
//  int32_t PHYLinkState = 0;
//  ETH_MACConfigTypeDef MACConf = {0};
  /* Start ETH HAL Init */
  MPU_Config();

  /* DEBUG: Verify structure layout to confirm mismatch */
  printf("DEBUG: sizeof(ETH_HandleTypeDef) = %u\n", (unsigned int)sizeof(ETH_HandleTypeDef));
  printf("DEBUG: ETH_RX_DESC_CNT (Local Define) = %u\n", ETH_RX_DESC_CNT);
  printf("DEBUG: offsetof(RxDescList) = %u\n", (unsigned int)offsetof(ETH_HandleTypeDef, RxDescList));
  printf("DEBUG: offsetof(RxDescIdx) = %u\n", (unsigned int)offsetof(ETH_HandleTypeDef, RxDescList.RxDescIdx));

   static uint8_t MACAddr[6] ;
  heth.Instance = ETH;
  MACAddr[0] = 0x00;
  MACAddr[1] = 0x80;
  MACAddr[2] = 0xE1;
  MACAddr[3] = 0x00;
  MACAddr[4] = 0x00;
  MACAddr[5] = 0x00;
  heth.Init.MACAddr = &MACAddr[0];
 heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
 /* Suppress warning about packed structure pointer conversion */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
 heth.Init.TxDesc = (ETH_DMADescTypeDef *)DMATxDscrTab;
 heth.Init.RxDesc = (ETH_DMADescTypeDef *)DMARxDscrTab;
#pragma GCC diagnostic pop
 heth.Init.RxBuffLen = 1536;

 /* USER CODE END MACADDRESS */
 
 /* Disable ETH interrupt before installing handler */
 HAL_NVIC_DisableIRQ(ETH_IRQn);

 printf("Ethernet MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
         MACAddr[0], MACAddr[1], MACAddr[2], MACAddr[3], MACAddr[4], MACAddr[5]);

  /* ETH_CODE: Zero out descriptor and RX pool memory before use */
  memset(DMARxDscrTab, 0, ETH_RX_DESC_CNT * sizeof(ETH_DMADescTypeDef_Shadow));
  memset(DMATxDscrTab, 0, ETH_TX_DESC_CNT * sizeof(ETH_DMADescTypeDef_Shadow));
  memset((void *)RX_POOL_BASE_ADDR, 0, ETH_RX_BUFFER_CNT * (sizeof(RxBuff_t)));

  /* ETH_CODE: Initialize the RX POOL before calling HAL_ETH_Init */
  printf("Initializing RX pool at 0x%08lx...\n", (unsigned long)RX_POOL_BASE_ADDR);
  LWIP_MEMPOOL_INIT(RX_POOL);

  hal_eth_init_status = HAL_ETH_Init(&heth);
  
  if (hal_eth_init_status == HAL_OK) {
    /* Register callbacks required for HAL_ETH_ReadData and Transmit */
    HAL_ETH_RegisterRxAllocateCallback(&heth, HAL_ETH_RxAllocateCallback);
    HAL_ETH_RegisterTxFreeCallback(&heth, HAL_ETH_TxFreeCallback);
    HAL_ETH_RegisterRxLinkCallback(&heth, HAL_ETH_RxLinkCallback);

    /* ETH_CODE: Manually commit descriptor list addresses and lengths to hardware registers.
     * This is critical because the HAL version in this environment lacks 
     * HAL_ETH_DMARxDescListInit and doesn't appear to commit them in HAL_ETH_Init. */
    heth.Instance->DMACRDLAR = (uint32_t)heth.Init.RxDesc;
    heth.Instance->DMACRDRLR = ETH_RX_DESC_CNT - 1; /* Ring Length (N-1) */
    
    heth.Instance->DMACTDLAR = (uint32_t)heth.Init.TxDesc;
    heth.Instance->DMACTDRLR = ETH_TX_DESC_CNT - 1; /* Ring Length (N-1) */
    
    __DSB();

    /* ETH_CODE: Manually initialize the internal HAL descriptor list wrappers.
     * The H7 HAL uses these trackers for ReadData/Transmit operations. 
     * We must populate build indices as well for HAL_ETH_ReadData to function. */
     memset(&heth.RxDescList, 0, sizeof(ETH_RxDescListTypeDef));
     
     /* ETH_CODE: Initialize Start and End pointers for RxDescList 
      * This is crucial for DMACRDTPR (Tail Pointer) updates to work correctly! */
     heth.RxDescList.pRxStart = (ETH_DMADescTypeDef *)DMARxDscrTab;
     heth.RxDescList.pRxEnd   = (ETH_DMADescTypeDef *)&DMARxDscrTab[ETH_RX_DESC_CNT - 1];
     
     heth.RxDescList.RxDescCnt = ETH_RX_DESC_CNT;
     heth.RxDescList.RxDescIdx = 0;
     heth.RxDescList.RxBuildDescIdx = 0;
    heth.RxDescList.RxBuildDescCnt = 0; /* Will be incremented during buffer allocation */

    for(int i = 0; i < ETH_RX_DESC_CNT; i++) {
        heth.RxDescList.RxDesc[i] = (uint32_t)&DMARxDscrTab[i];
    }
    
    memset(&heth.TxDescList, 0, sizeof(ETH_TxDescListTypeDef));
    heth.TxDescList.CurTxDesc = 0;
    for(int i = 0; i < ETH_TX_DESC_CNT; i++) {
        heth.TxDescList.TxDesc[i] = (uint32_t)&DMATxDscrTab[i];
    }




    printf("ETH: DMA Descriptors committed to hardware: RXaddr=0x%08lx, RXlen=%lu, TXaddr=0x%08lx, TXlen=%lu\n",
           (unsigned long)heth.Instance->DMACRDLAR, (unsigned long)(heth.Instance->DMACRDRLR + 1),
           (unsigned long)heth.Instance->DMACTDLAR, (unsigned long)(heth.Instance->DMACTDRLR + 1));


    printf("Installing ETH interrupt handler for vector %lu...\n", (unsigned long)ETH_IRQn);
    rtems_status_code sc = rtems_interrupt_handler_install(
      ETH_IRQn,
      "ETH",
      RTEMS_INTERRUPT_UNIQUE,
      stm32h7_eth_interrupt_handler,
      NULL
    );
    if (sc != RTEMS_SUCCESSFUL) {
      printf("ERROR: Failed to install ETH interrupt handler: %d\n", (int)sc);
      hal_eth_init_status = HAL_ERROR;
    } else {
      printf("ETH interrupt handler installed successfully\n");
      /* ETH_CODE: Explicitly enable the interrupt vector in RTEMS */
      rtems_interrupt_vector_enable(ETH_IRQn);

      /* Enable ETH interrupt after successful handler installation */
      HAL_NVIC_SetPriority(ETH_IRQn, 0x5, 0); /* Slightly higher priority */
      HAL_NVIC_EnableIRQ(ETH_IRQn);
      
      /* diagnostic: check if IRQ is enabled */
      printf("ETH: IRQ %lu enabled in NVIC\n", (unsigned long)ETH_IRQn);
    }
  }

  memset(&TxConfig, 0 , sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;

  /* End ETH HAL Init */

  /* ETH_CODE: RX pool initialization moved before HAL_ETH_Init */

#if LWIP_ARP || LWIP_ETHERNET

  printf("Setting MAC address...\n");
  /* set MAC hardware address length */
  netif->hwaddr_len = ETH_HWADDR_LEN;

  /* set MAC hardware address */
  netif->hwaddr[0] =  heth.Init.MACAddr[0];
  netif->hwaddr[1] =  heth.Init.MACAddr[1];
  netif->hwaddr[2] =  heth.Init.MACAddr[2];
  netif->hwaddr[3] =  heth.Init.MACAddr[3];
  netif->hwaddr[4] =  heth.Init.MACAddr[4];
  netif->hwaddr[5] =  heth.Init.MACAddr[5];

  /* maximum transfer unit */
  netif->mtu = ETH_MAX_PAYLOAD;

  /* Accept broadcast address and ARP traffic */
  /* don't set NETIF_FLAG_ETHARP if this device is not an ethernet one */
  #if LWIP_ARP
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
  #else
    netif->flags |= NETIF_FLAG_BROADCAST;
  #endif /* LWIP_ARP */

  printf("Creating RxPktSemaphore...\n");
  /* create a binary semaphore used for informing ethernetif of frame reception */
  if (sys_sem_new(&RxPktSemaphore, 0) != ERR_OK) {
        /* Handle error */
        printf("ERROR: Failed to create RxPktSemaphore\n");
  }

  printf("Creating TxPktSemaphore...\n");
  /* create a binary semaphore used for informing ethernetif of frame transmission */
  if (sys_sem_new(&TxPktSemaphore, 0) != ERR_OK) {
        /* Handle error */
        printf("ERROR: Failed to create TxPktSemaphore\n");
  }

  /* NOTE: Ethernet threads will be created AFTER PHY init to avoid race conditions */

/* USER CODE BEGIN PHY_PRE_CONFIG */

/* USER CODE END PHY_PRE_CONFIG */
  printf("Registering PHY IO...\n");
  printf("LAN8742 object at: 0x%p\n", &LAN8742);
  printf("IOCtx at: 0x%p\n", &LAN8742_IOCtx);
  printf("IOCtx.Init = 0x%p\n", LAN8742_IOCtx.Init);
  printf("IOCtx.DeInit = 0x%p\n", LAN8742_IOCtx.DeInit);
  printf("IOCtx.WriteReg = 0x%p\n", LAN8742_IOCtx.WriteReg);
  printf("IOCtx.ReadReg = 0x%p\n", LAN8742_IOCtx.ReadReg);
  printf("IOCtx.GetTick = 0x%p\n", LAN8742_IOCtx.GetTick);
  
  /* Test HAL_GetTick before PHY init */
  printf("Testing HAL_GetTick: %lu\n", (unsigned long)HAL_GetTick());
  rtems_task_wake_after(RTEMS_MILLISECONDS_TO_TICKS(100));
  printf("After 100ms delay, HAL_GetTick: %lu\n", (unsigned long)HAL_GetTick());
  
  /* Set PHY IO functions  */
  LAN8742.DevAddr = 0;
  LAN8742.Is_Initialized = 0;
  LAN8742_RegisterBusIO(&LAN8742, &LAN8742_IOCtx);
  LAN8742_Init(&LAN8742);

  if (hal_eth_init_status != HAL_OK)
  {
    Error_Handler();
  }
#endif /* LWIP_ARP || LWIP_ETHERNET */

/* USER CODE BEGIN LOW_LEVEL_INIT */
  
  /* Create the Ethernet threads. Hardware will be started by EthLink thread. */
  printf("Creating Ethernet threads...\n");
  sys_thread_new("EthIf", ethernetif_input, netif, INTERFACE_THREAD_STACK_SIZE, TCPIP_THREAD_PRIO);
  sys_thread_new("EthLink", ethernet_link_thread, netif, INTERFACE_THREAD_STACK_SIZE, TCPIP_THREAD_PRIO);

/* USER CODE END LOW_LEVEL_INIT */
}

/**
 * This function should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @param p the MAC packet to send (e.g. IP packet including MAC addresses and type)
 * @return ERR_OK if the packet could be sent
 *         an err_t value if the packet couldn't be sent
 *
 * @note Returning ERR_MEM here if a DMA queue of your MAC is full can lead to
 *       strange results. You might consider waiting for space in the DMA queue
 *       to become available since the stack doesn't retry to send a packet
 *       dropped because of memory failure (except for the TCP timers).
 */

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
  struct pbuf *q = NULL;
  err_t errval = ERR_OK;
  ETH_BufferTypeDef Txbuffer[ETH_TX_DESC_CNT];
  uint8_t *tx_bounce_buffer = (uint8_t *)ETH_TX_BUFFER_ADDR;
  uint32_t total_len = 0;

  printf("\n========== TX START ==========\n");
  printf("TX: low_level_output called, p=0x%p, tot_len=%u\n", p, (unsigned int)p->tot_len);
  
  if (p->tot_len > ETH_TX_BUFFER_MAX_SIZE) {
      printf("TX ERROR: Packet too large for bounce buffer (%u > %u)\n", (unsigned int)p->tot_len, ETH_TX_BUFFER_MAX_SIZE);
      return ERR_BUF;
  }

  memset(Txbuffer, 0 , ETH_TX_DESC_CNT*sizeof(ETH_BufferTypeDef));

  /* ETH_CODE: Flatten pbuf chain into D2 SRAM bounce buffer */
  for(q = p; q != NULL; q = q->next)
  {
    memcpy(tx_bounce_buffer + total_len, q->payload, q->len);
    printf("TX: Copied %u bytes from 0x%p to 0x%p (Offset %lu)\n", 
           (unsigned int)q->len, q->payload, (tx_bounce_buffer + total_len), (unsigned long)total_len);
    total_len += q->len;
  }

  /* ETH_CODE: Insane Logging - Hex Dump of Bounce Buffer */
  printf("TX DATA DUMP (%lu bytes):\n", (unsigned long)total_len);
  for(uint32_t k=0; k<total_len; k++) {
       if(k%16 == 0) printf("\n  %04x: ", (unsigned int)k);
       printf("%02x ", tx_bounce_buffer[k]);
  }
  printf("\n\n");

  /* ETH_CODE: Setup TxConfig to use the single bounce buffer */
  Txbuffer[0].buffer = tx_bounce_buffer;
  Txbuffer[0].len = total_len;
  Txbuffer[0].next = NULL;

  TxConfig.Length = total_len;
  TxConfig.TxBuffer = Txbuffer;
  TxConfig.pData = p;

  pbuf_ref(p);

  /* ETH_CODE: Clean D2 Cache for the bounce buffer to ensure DMA sees the data */
  SCB_CleanDCache_by_Addr((uint32_t *)tx_bounce_buffer, total_len);

  /* Capture current descriptor index before HAL increments it */
  uint32_t desc_idx = heth.TxDescList.CurTxDesc;

  printf("TX: Calling HAL_ETH_Transmit_IT (Desc Index %lu)\n", (unsigned long)desc_idx);
  
  HAL_ETH_Transmit_IT(&heth, &TxConfig);
  
  /* ETH_CODE: Insane Logging - Dump Descriptor AFTER HAL setup but BEFORE DMA completion (mostly) */
  ETH_DMADescTypeDef_Shadow *d = &DMATxDscrTab[desc_idx];
  printf("TX: Descriptor [%lu] State AFTER HAL Setup:\n", (unsigned long)desc_idx);
  printf("    DESC0 (Addr) = 0x%08lx (Should be 0x%08lx)\n", (unsigned long)d->DESC0, (unsigned long)ETH_TX_BUFFER_ADDR);
  printf("    DESC1        = 0x%08lx\n", (unsigned long)d->DESC1);
  printf("    DESC2 (Len)  = 0x%08lx\n", (unsigned long)d->DESC2);
  printf("    DESC3 (Ctrl) = 0x%08lx (OWN bit should be 1)\n", (unsigned long)d->DESC3);
  
  printf("TX: DMA Registers:\n");
  printf("    DMACSR   = 0x%08lx\n", (unsigned long)heth.Instance->DMACSR);
  printf("    DMACTDTPR = 0x%08lx\n", (unsigned long)heth.Instance->DMACTDTPR);

  printf("TX: Waiting for TxPktSemaphore...\n");
  sys_arch_sem_wait(&TxPktSemaphore, TIME_WAITING_FOR_INPUT);
  printf("TX: TxPktSemaphore acquired\n");

  /* ETH_CODE: Insane Logging - Dump Descriptor AFTER DMA Completion */
  printf("TX: Descriptor [%lu] State AFTER DMA Completion:\n", (unsigned long)desc_idx);
  printf("    DESC0 (Addr) = 0x%08lx\n", (unsigned long)d->DESC0);
  printf("    DESC3 (Stat) = 0x%08lx (OWN bit should be 0)\n", (unsigned long)d->DESC3);

  HAL_ETH_ReleaseTxPacket(&heth);
  printf("TX: Packet released\n");
  printf("========== TX END ==========\n\n");

  return errval;
}

/**
 * Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return a pbuf filled with the received packet (including MAC header)
 *         NULL on memory error
   */
/* ETH_CODE: Function to rebuild RX descriptors and return them to DMA
 * This is critical for preventing "Receive Buffer Unavailable" errors.
 * After HAL_ETH_ReadData processes a packet, the descriptor must be
 * returned to the DMA with a new buffer and OWN bit set. */
static void __attribute__((unused)) ethernetif_rebuild_rx_descriptors(void)
{
  /* Use RxBuildDescIdx (Refill Pointer) to start searching for empty descriptors.
   * Using RxDescIdx (Read Pointer) is wrong because passing the read pointer
   * skips the descriptor we just consumed! */
  uint32_t idx = heth.RxDescList.RxBuildDescIdx;
  
  printf("Rebuild Loop: Start Idx=%lu\n", (unsigned long)idx);
  
  /* Rebuild descriptors that have been processed (OWN=0) */
  while (1) {
    /* ETH_CODE: Invalidate Cache to ensure we read actual RAM value */
    SCB_InvalidateDCache_by_Addr((uint32_t *)&DMARxDscrTab[idx], sizeof(ETH_DMADescTypeDef_Shadow));
    
    if ((DMARxDscrTab[idx].DESC3 & 0x80000000) != 0) {
      /* Descriptor is still owned by DMA (OWN=1). We caught up. */
       printf("Rebuild: Idx=%lu is DMA owned (DESC3=0x%08lx). Stop.\n",
              (unsigned long)idx, (unsigned long)DMARxDscrTab[idx].DESC3);
       break;
    }
    
    /* ETH_CODE: Handle Context Descriptors (CTXT=1)
     * Context descriptors don't have packet data and should be recycled directly.
     * They have OWN=0 and CTXT=1 but don't have FD (First Desc) or LD (Last Desc) set
     * for packet data. */
    if ((DMARxDscrTab[idx].DESC3 & 0x40000000) != 0) {
        printf("Rebuild: Idx=%lu is Context Descriptor (CTXT=1), recycling...\n", (unsigned long)idx);
        /* Allocate a new buffer and recycle this descriptor */
        uint8_t *ptr = NULL;
        HAL_ETH_RxAllocateCallback(&ptr);
        if (ptr) {
            DMARxDscrTab[idx].DESC0 = (uint32_t)ptr;
            DMARxDscrBackup[idx] = (uint32_t)ptr;
            DMARxDscrTab[idx].DESC2 = (heth.Init.RxBuffLen & 0x3FFF);
            DMARxDscrTab[idx].DESC3 = 0x80000000 | 0x01000000; /* OWN + BUF1V */
            SCB_CleanDCache_by_Addr((uint32_t *)&DMARxDscrTab[idx], sizeof(ETH_DMADescTypeDef_Shadow));
            __DSB();
            heth.RxDescList.RxBuildDescCnt++;
            heth.RxDescList.RxBuildDescIdx = (idx + 1) % ETH_RX_DESC_CNT;
        }
        idx = (idx + 1) % ETH_RX_DESC_CNT;
        if (idx == heth.RxDescList.RxBuildDescIdx) break;
        continue;
    }

    uint8_t *ptr = NULL;
    
    /* Allocate a new buffer for this descriptor */
    HAL_ETH_RxAllocateCallback(&ptr);
    
    if (ptr != NULL) {
      /* Set the new buffer address */
      DMARxDscrTab[idx].DESC0 = (uint32_t)ptr;
      DMARxDscrBackup[idx] = (uint32_t)ptr;
      
      /* Set buffer length */
      DMARxDscrTab[idx].DESC2 = (heth.Init.RxBuffLen & 0x3FFF);
      
      /* Return descriptor to DMA: set OWN and BUF1V bits */
      DMARxDscrTab[idx].DESC3 = 0x80000000 | 0x01000000;
      
      /* Ensure memory write is visible to DMA */
      SCB_CleanDCache_by_Addr((uint32_t *)&DMARxDscrTab[idx], sizeof(ETH_DMADescTypeDef_Shadow));
      __DSB();
      
      /* Update HAL tracking (cap at descriptor count) */
      if (heth.RxDescList.RxBuildDescCnt < ETH_RX_DESC_CNT) {
          heth.RxDescList.RxBuildDescCnt++;
      }
      heth.RxDescList.RxBuildDescIdx = (idx + 1) % ETH_RX_DESC_CNT;
      
      printf("Rebuilt RX desc %lu: addr=0x%08lx, DESC3=0x%08lx (Cnt=%lu)\n",
             (unsigned long)idx, (unsigned long)DMARxDscrTab[idx].DESC0,
             (unsigned long)DMARxDscrTab[idx].DESC3, 
             (unsigned long)heth.RxDescList.RxBuildDescCnt);
    } else {
      printf("ERROR: Failed to allocate buffer for RX descriptor rebuild %lu\n", (unsigned long)idx);
      RxAllocStatus = RX_ALLOC_ERROR;
      break;
    }
    
    /* Move to next descriptor */
    idx = (idx + 1) % ETH_RX_DESC_CNT;
    
    /* Safety check involved in 'while(1)' loop */
    if (idx == heth.RxDescList.RxBuildDescIdx) break; 
  }
  
  /* Update Tail Pointer to point behind current read pointer */
  stm32h7_eth_kick_rx_dma();
}

/**
 * Helper function to process/advance past a descriptor (packet or context)
 * This is called when we have a CPU-owned descriptor that needs recycling
 */
static void stm32h7_recycle_rx_descriptor(uint32_t idx)
{
    printf("\n========== RECYCLE RX DESC %lu ==========\n", (unsigned long)idx);
    ETH_DMADescTypeDef_Shadow *d = &DMARxDscrTab[idx];
    
    printf("RECYCLE: Before - DESC0=0x%08lx, DESC1=0x%08lx, DESC2=0x%08lx, DESC3=0x%08lx\n",
           (unsigned long)d->DESC0, (unsigned long)d->DESC1,
           (unsigned long)d->DESC2, (unsigned long)d->DESC3);
    printf("RECYCLE: RxDescIdx=%lu, RxBuildDescIdx=%lu, RxBuildDescCnt=%lu\n",
           (unsigned long)heth.RxDescList.RxDescIdx,
           (unsigned long)heth.RxDescList.RxBuildDescIdx,
           (unsigned long)heth.RxDescList.RxBuildDescCnt);
    
    /* Allocate new buffer for this descriptor */
    uint8_t *new_ptr = NULL;
    HAL_ETH_RxAllocateCallback(&new_ptr);
    
    if (new_ptr) {
        printf("RECYCLE: Allocated new buffer at 0x%08lx\n", (unsigned long)new_ptr);
        /* Update Descriptor */
        d->DESC0 = (uint32_t)new_ptr;
        DMARxDscrBackup[idx] = (uint32_t)new_ptr;
        d->DESC2 = (heth.Init.RxBuffLen & 0x3FFF);
        
        /* Ownership back to DMA + IOC + BUF1V */
        d->DESC3 = 0x80000000 | 0x40000000 | 0x01000000;
        
        printf("RECYCLE: After - DESC0=0x%08lx, DESC1=0x%08lx, DESC2=0x%08lx, DESC3=0x%08lx\n",
               (unsigned long)d->DESC0, (unsigned long)d->DESC1,
               (unsigned long)d->DESC2, (unsigned long)d->DESC3);
        
        /* Flush Cache */
        SCB_CleanDCache_by_Addr((uint32_t *)d, sizeof(ETH_DMADescTypeDef_Shadow));
        __DSB();
        
        /* Update HAL counters */
        if (heth.RxDescList.RxBuildDescCnt < ETH_RX_DESC_CNT) {
            heth.RxDescList.RxBuildDescCnt++;
        }
        heth.RxDescList.RxBuildDescIdx = (idx + 1) % ETH_RX_DESC_CNT;
        heth.RxDescList.RxDescIdx = (idx + 1) % ETH_RX_DESC_CNT;
        
        printf("RECYCLE: After update - RxDescIdx=%lu, RxBuildDescIdx=%lu, RxBuildDescCnt=%lu\n",
               (unsigned long)heth.RxDescList.RxDescIdx,
               (unsigned long)heth.RxDescList.RxBuildDescIdx,
               (unsigned long)heth.RxDescList.RxBuildDescCnt);
        
        /* Kick DMA Tail Pointer */
        stm32h7_eth_kick_rx_dma();
        printf("========== RECYCLE RX DESC END ==========\n\n");
    } else {
        printf("CRITICAL: Failed to recycle RX descriptor %lu\n", (unsigned long)idx);
        printf("========== RECYCLE RX DESC END (FAILED) ==========\n\n");
    }
}

/* Helper function to safely get pbuf from buffer address */
static struct pbuf *stm32h7_get_pbuf_from_buff(uint8_t *buff)
{
    /* Calculate pbuf address: buff is at offsetof(RxBuff_t, buff) within RxBuff_t */
    struct pbuf *p = (struct pbuf *)((uint8_t *)buff - offsetof(RxBuff_t, buff));
    return p;
}

static struct pbuf * low_level_input(struct netif *netif)
{
  struct pbuf *p = NULL;

  /* ETH_CODE: Loop to skip non-packet descriptors (Context/Strange)
   * and find the next valid packet. Returning NULL prematurely causes
   * the input thread to sleep even if valid packets follow.
   */
  while (1) {
      uint32_t idx = heth.RxDescList.RxDescIdx;
      ETH_DMADescTypeDef_Shadow *d = &DMARxDscrTab[idx];

      /* ETH_CODE: Invalidate Cache BEFORE reading the descriptor! */
      SCB_InvalidateDCache_by_Addr((uint32_t *)d, sizeof(ETH_DMADescTypeDef_Shadow));

      /* Check if the descriptor is owned by CPU (Bit 31 = 0) */
      if ((d->DESC3 & 0x80000000) != 0) {
          /* Descriptor is DMA-owned (OWN=1), no more packets available */
          return NULL;
      }

      /* Handle Context Descriptors (CTXT=1, Bit 30) */
      if ((d->DESC3 & 0x40000000) != 0) {
          printf("LLI: Context Descriptor detected (CTXT=1), recycling...\n");
          stm32h7_recycle_rx_descriptor(idx);
          /* Continue loop to check next descriptor immediately */
          continue;
      }

      /* CPU owned + Normal Descriptor */
      if ((d->DESC3 & 0x20000000) && (d->DESC3 & 0x10000000)) {
           /* First Desc (FD) + Last Desc (LD) set -> Single packet */
           uint32_t pkt_len = (d->DESC3 & 0x00007FFF);
           printf("LLI: Single packet detected, len=%lu bytes\n", (unsigned long)pkt_len);

           /* Get the pbuf from the back-up address */
           if (DMARxDscrBackup[idx] != 0) {
               uint8_t *buff_with_offset = (uint8_t *)DMARxDscrBackup[idx];
               uint8_t *actual_buff = buff_with_offset - 2;
               printf("LLI: Buffer address: backup=0x%08lx, actual=0x%08lx\n",
                      (unsigned long)DMARxDscrBackup[idx], (unsigned long)actual_buff);

               struct pbuf *p_manual = stm32h7_get_pbuf_from_buff(actual_buff);
               struct pbuf_custom *p_custom = (struct pbuf_custom *)p_manual;

               /* ETH_CODE: Use pbuf_alloced_custom with actual buffer start.
                * We include ETH_PAD_SIZE (2 bytes) in the length so LwIP can strip it.
                * Note: We do NOT manually adjust p->payload here. LwIP's ethernet_input
                * will do pbuf_header(p, -ETH_PAD_SIZE) to skip the padding. */
               p_custom->custom_free_function = pbuf_free_custom;
               p = pbuf_alloced_custom(PBUF_RAW, pkt_len + 2, PBUF_REF, p_custom, actual_buff, ETH_RX_BUFFER_SIZE);

               if (p != NULL) {
                   SCB_InvalidateDCache_by_Addr((uint32_t *)actual_buff, pkt_len);
                   printf("LLI: pbuf created successfully, p=0x%p, payload=0x%p (Padding included)\n", p, p->payload);
                   
                   /* Dump packet content */
                   printf("PKT DUMP (%lu bytes):", (unsigned long)pkt_len);
                   /* ETH_CODE: Skip padding for dump visualization */
                   uint8_t *pd = (uint8_t *)p->payload + 2;
                   for(uint32_t k=0; k<pkt_len; k++) {
                       if(k%16 == 0) printf("\n  %04x: ", (unsigned int)k);
                       printf("%02x ", pd[k]);
                   }
                   printf("\n");

                   uint8_t *eth_hdr = (uint8_t *)p->payload + 2;
                   printf("LLI: Ethernet Header: %02x:%02x:%02x:%02x:%02x:%02x -> %02x:%02x:%02x:%02x:%02x:%02x, type=0x%02x%02x\n",
                          eth_hdr[0], eth_hdr[1], eth_hdr[2], eth_hdr[3], eth_hdr[4], eth_hdr[5],
                          eth_hdr[6], eth_hdr[7], eth_hdr[8], eth_hdr[9], eth_hdr[10], eth_hdr[11],
                          eth_hdr[12], eth_hdr[13]);
               } else {
                   printf("CRITICAL: pbuf_alloced_custom failed! Dropping packet.\n");
               }

               /* Refill descriptor */
               uint8_t *new_ptr = NULL;
               HAL_ETH_RxAllocateCallback(&new_ptr);

               if (new_ptr) {
                   printf("LLI: Refilling descriptor %lu with new buffer 0x%08lx\n",
                          (unsigned long)idx, (unsigned long)new_ptr);
                   d->DESC0 = (uint32_t)new_ptr;
                   DMARxDscrBackup[idx] = (uint32_t)new_ptr;
                   d->DESC2 = (heth.Init.RxBuffLen & 0x3FFF);
                   d->DESC3 = 0x80000000 | 0x40000000 | 0x01000000;
                   SCB_CleanDCache_by_Addr((uint32_t *)d, sizeof(ETH_DMADescTypeDef_Shadow));
                   __DSB();

                   if (heth.RxDescList.RxBuildDescCnt < ETH_RX_DESC_CNT) {
                       heth.RxDescList.RxBuildDescCnt++;
                   }
                   heth.RxDescList.RxBuildDescIdx = (idx + 1) % ETH_RX_DESC_CNT;
                   
                   printf("LLI: After refill - RxDescIdx=%lu, RxBuildDescIdx=%lu, RxBuildDescCnt=%lu\n",
                          (unsigned long)heth.RxDescList.RxDescIdx,
                          (unsigned long)heth.RxDescList.RxBuildDescIdx,
                          (unsigned long)heth.RxDescList.RxBuildDescCnt);
                   
                   stm32h7_eth_kick_rx_dma();
               } else {
                   printf("CRITICAL: Failed to refill Rx Desc %lu\n", (unsigned long)idx);
               }

               /* Update HAL Read Index to next descriptor */
               heth.RxDescList.RxDescIdx = (idx + 1) % ETH_RX_DESC_CNT;
               
               /* Return the packet we found */
               return p;
           }
      } else {
           /* Multi-fragment packet or strange state. Recycling. */
           printf("LLI: Fragmented/Strange packet (DESC3=0x%08lx). Recycling.\n", (unsigned long)d->DESC3);
           stm32h7_recycle_rx_descriptor(idx);
           /* Continue loop to check next descriptor */
           continue;
      }
  }
  return p;
}

/**
 * This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface. Then the type of the received packet is determined and
 * the appropriate input function is called.
 *
 * @param netif the lwip network interface structure for this ethernetif
 */
void ethernetif_input(void* argument)
{
  struct pbuf *p = NULL;
  struct netif *netif = (struct netif *) argument;

  printf("ethernetif_input thread started, netif=0x%p\n", netif);

  for( ;; )
  {
    if (sys_arch_sem_wait(&RxPktSemaphore, TIME_WAITING_FOR_INPUT) != SYS_ARCH_TIMEOUT)
    {
      // printf("\n========== ETHERNETIF_INPUT: SEMAPHORE SIGNALED ==========\n");
      do
      {
        p = low_level_input( netif );
        if (p != NULL)
        {
          printf("ETHERNETIF_INPUT: Got packet %u bytes, passing to netif->input\n", (unsigned int)p->tot_len);
          if (netif->input( p, netif) != ERR_OK )
          {
            printf("ETHERNETIF_INPUT: netif->input failed, freeing pbuf\n");
            pbuf_free(p);
          } else {
            printf("ETHERNETIF_INPUT: netif->input succeeded\n");
          }
        }
      } while(p!=NULL);
      // printf("========== ETHERNETIF_INPUT: PROCESSING COMPLETE ==========\n\n");
    }
  }
}

#if !LWIP_ARP
/**
 * This function has to be completed by user in case of ARP OFF.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if ...
 */
static err_t low_level_output_arp_off(struct netif *netif, struct pbuf *q, const ip4_addr_t *ipaddr)
{
  err_t errval;
  errval = ERR_OK;

/* USER CODE BEGIN 5 */

/* USER CODE END 5 */

  return errval;

}
#endif /* LWIP_ARP */

/**
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 * This function should be passed as a parameter to netif_add().
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if the loopif is initialized
 *         ERR_MEM if private data couldn't be allocated
 *         any other err_t on error
 */
err_t ethernetif_init(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
  /* Initialize interface hostname */
  netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

  /*
   * Initialize the snmp variables and counters inside the struct netif.
   * The last argument should be replaced with your link speed, in units
   * of bits per second.
   */
  // MIB2_INIT_NETIF(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED_OF_YOUR_NETIF_IN_BPS);

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
  /* We directly use etharp_output() here to save a function call.
   * You can instead declare your own function an call etharp_output()
   * from it if you have to do some checks before sending (e.g. if link
   * is available...) */

#if LWIP_IPV4
#if LWIP_ARP || LWIP_ETHERNET
#if LWIP_ARP
  netif->output = etharp_output;
#else
  /* The user should write its own code in low_level_output_arp_off function */
  netif->output = low_level_output_arp_off;
#endif /* LWIP_ARP */
#endif /* LWIP_ARP || LWIP_ETHERNET */
#endif /* LWIP_IPV4 */

#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
#endif /* LWIP_IPV6 */

  netif->linkoutput = low_level_output;

  /* initialize the hardware */
  low_level_init(netif);

  return ERR_OK;
}

/**
  * @brief  Custom Rx pbuf free callback
  * @param  pbuf: pbuf to be freed
  * @retval None
  */
void pbuf_free_custom(struct pbuf *p)
{
  printf("\n========== PBUF_FREE_CUSTOM ==========\n");
  printf("PBUF_FREE: p=0x%p, ref=%u, tot_len=%u\n", p, (unsigned int)p->ref, (unsigned int)p->tot_len);
  struct pbuf_custom* custom_pbuf = (struct pbuf_custom*)p;
  printf("PBUF_FREE: custom_pbuf=0x%p, freeing to RX_POOL\n", custom_pbuf);
  LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf);

  /* If the Rx Buffer Pool was exhausted, signal the ethernetif_input task to
   * call HAL_ETH_GetRxDataBuffer to rebuild the Rx descriptors. */

  if (RxAllocStatus == RX_ALLOC_ERROR)
  {
    printf("PBUF_FREE: RxAllocStatus was ERROR, signaling semaphore\n");
    RxAllocStatus = RX_ALLOC_OK;
    sys_sem_signal(&RxPktSemaphore);
  }
  printf("========== PBUF_FREE_CUSTOM END ==========\n\n");
}

/* USER CODE BEGIN 6 */
/* USER CODE END 6 */

void HAL_ETH_MspInit(ETH_HandleTypeDef* ethHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(ethHandle->Instance==ETH)
  {
  /* USER CODE BEGIN ETH_MspInit 0 */

  /* USER CODE END ETH_MspInit 0 */
    /* Enable SYSCFG clock for Ethernet RMII/MII selection */
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    
    /* Select RMII Interface BEFORE enabling ETH clocks */
    HAL_SYSCFG_ETHInterfaceSelect(SYSCFG_ETH_RMII);

    /* ETH_CODE: Enable I/O Compensation Cell for high-speed GPIO (RMII) */
    HAL_EnableCompensationCell();
    
    /* Wait for I/O Compensation Cell to be ready (with timeout to prevent hang) */
    uint32_t timeout = 0xFFFF;
    while (!(SYSCFG->CCCSR & SYSCFG_CCCSR_READY) && timeout--);
    
    if (timeout == 0) {
      printf("WARNING: I/O Compensation Cell not ready!\n");
    }

    /* Enable Peripheral clock */
    __HAL_RCC_ETH1MAC_CLK_ENABLE();
    __HAL_RCC_ETH1TX_CLK_ENABLE();
    __HAL_RCC_ETH1RX_CLK_ENABLE();
    
    /* Small delay for clocks and RMII data path to stabilize */
    for(volatile int i = 0; i < 100000; i++);


    /* ETH_CODE: Enable D2 SRAM clocks for descriptors and buffers */
    __HAL_RCC_D2SRAM1_CLK_ENABLE();
    __HAL_RCC_D2SRAM2_CLK_ENABLE();
    __HAL_RCC_D2SRAM3_CLK_ENABLE();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    /**ETH GPIO Configuration
    PC1     ------> ETH_MDC
    PA1     ------> ETH_REF_CLK
    PA2     ------> ETH_MDIO
    PA7     ------> ETH_CRS_DV
    PC4     ------> ETH_RXD0
    PC5     ------> ETH_RXD1
    PB13     ------> ETH_TXD1
    PG11     ------> ETH_TX_EN
    PG13     ------> ETH_TXD0
    */
    GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* Peripheral interrupt init - moved to low_level_init() after handler installation */
    /* NVIC configuration is now done in low_level_init() to ensure proper sequence */
  /* USER CODE BEGIN ETH_MspInit 1 */

  /* USER CODE END ETH_MspInit 1 */
  }
}

void HAL_ETH_MspDeInit(ETH_HandleTypeDef* ethHandle)
{
  if(ethHandle->Instance==ETH)
  {
  /* USER CODE BEGIN ETH_MspDeInit 0 */

  /* USER CODE END ETH_MspDeInit 0 */
    /* Disable Peripheral clock */
    __HAL_RCC_ETH1MAC_CLK_DISABLE();
    __HAL_RCC_ETH1TX_CLK_DISABLE();
    __HAL_RCC_ETH1RX_CLK_DISABLE();

    /**ETH GPIO Configuration
    PC1     ------> ETH_MDC
    PA1     ------> ETH_REF_CLK
    PA2     ------> ETH_MDIO
    PA7     ------> ETH_CRS_DV
    PC4     ------> ETH_RXD0
    PC5     ------> ETH_RXD1
    PB13     ------> ETH_TXD1
    PG11     ------> ETH_TX_EN
    PG13     ------> ETH_TXD0
    */
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5);

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_7);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_13);

    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_11|GPIO_PIN_13);

    /* Peripheral interrupt Deinit*/
    HAL_NVIC_DisableIRQ(ETH_IRQn);

  /* USER CODE BEGIN ETH_MspDeInit 1 */

  /* USER CODE END ETH_MspDeInit 1 */
  }
}

/*******************************************************************************
                       PHI IO Functions
*******************************************************************************/
/**
  * @brief  Initializes the MDIO interface GPIO and clocks.
  * @param  None
  * @retval 0 if OK, -1 if ERROR
  */
int32_t ETH_PHY_IO_Init(void)
{
  /* We assume that MDIO GPIO configuration is already done
     in the ETH_MspInit() else it should be done here
  */

  /* Configure the MDIO Clock */
  HAL_ETH_SetMDIOClockRange(&heth);

  return 0;
}

/**
  * @brief  De-Initializes the MDIO interface .
  * @param  None
  * @retval 0 if OK, -1 if ERROR
  */
int32_t ETH_PHY_IO_DeInit (void)
{
  return 0;
}

/**
  * @brief  Read a PHY register through the MDIO interface.
  * @param  DevAddr: PHY port address
  * @param  RegAddr: PHY register address
  * @param  pRegVal: pointer to hold the register value
  * @retval 0 if OK -1 if Error
  */
int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal)
{
  if(HAL_ETH_ReadPHYRegister(&heth, DevAddr, RegAddr, pRegVal) != HAL_OK)
  {
    return -1;
  }

  return 0;
}

/**
  * @brief  Write a value to a PHY register through the MDIO interface.
  * @param  DevAddr: PHY port address
  * @param  RegAddr: PHY register address
  * @param  RegVal: Value to be written
  * @retval 0 if OK -1 if Error
  */
int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal)
{
  if(HAL_ETH_WritePHYRegister(&heth, DevAddr, RegAddr, RegVal) != HAL_OK)
  {
    return -1;
  }

  return 0;
}

/**
  * @brief  Get the time in millisecons used for internal PHY driver process.
  * @retval Time value
  */
int32_t ETH_PHY_IO_GetTick(void)
{
  return HAL_GetTick();
}

/**
  * @brief  Check the ETH link state then update ETH driver and netif link accordingly.
  * @param  argument: netif
  * @retval None
  */
void ethernet_link_thread(void* argument)
{
  ETH_MACConfigTypeDef MACConf = {0};
//  int32_t PHYLinkState = 0;
//  uint32_t linkchanged = 0U, speed = 0U, duplex = 0U;
    uint32_t speed = 0U, duplex = 0U;
  struct netif *netif = (struct netif *) argument;

  printf("Ethernet link thread started\n");

  /* Initialize PHY asynchronously to avoid blocking the main task */
  printf("PHY: Resetting...\n");
  ETH_PHY_IO_WriteReg(0, 0, 0x8000); /* Soft reset */
  sys_msleep(100);
  
  printf("PHY: Enabling auto-negotiation...\n");
  ETH_PHY_IO_WriteReg(0, 0, 0x1200); /* Auto-neg + Restart */
  
  LAN8742.Is_Initialized = 1;

  for(;;)
  {
    /* Use direct BSR read while LAN8742_GetLinkState might be unreliable */
    uint32_t bsr = 0;
    ETH_PHY_IO_ReadReg(0, 1, &bsr);
    
    if (bsr & 0x0004) {
      /* Link is Up according to hardware */
      if (!netif_is_link_up(netif)) {
        printf("PHY: Link Up detected (BSR=0x%04lx)\n", (unsigned long)bsr);

        /* Get negotiated speed and duplex */
        int32_t linkState = LAN8742_GetLinkState(&LAN8742);
        switch (linkState) {
          case LAN8742_STATUS_100MBITS_FULLDUPLEX:
            speed = ETH_SPEED_100M;
            duplex = ETH_FULLDUPLEX_MODE;
            printf("Negotiated: 100Mbps Full Duplex\n");
            break;
          case LAN8742_STATUS_100MBITS_HALFDUPLEX:
            speed = ETH_SPEED_100M;
            duplex = ETH_HALFDUPLEX_MODE;
            printf("Negotiated: 100Mbps Half Duplex\n");
            break;
          case LAN8742_STATUS_10MBITS_FULLDUPLEX:
            speed = ETH_SPEED_10M;
            duplex = ETH_FULLDUPLEX_MODE;
            printf("Negotiated: 10Mbps Full Duplex\n");
            break;
          case LAN8742_STATUS_10MBITS_HALFDUPLEX:
            speed = ETH_SPEED_10M;
            duplex = ETH_HALFDUPLEX_MODE;
            printf("Negotiated: 10Mbps Half Duplex\n");
            break;
          case LAN8742_STATUS_AUTONEGO_NOTDONE:
            printf("Auto-negotiation not done yet\n");
            /* Fall back to 100M Full Duplex */
            speed = ETH_SPEED_100M;
            duplex = ETH_FULLDUPLEX_MODE;
            break;
          default:
            printf("Unknown link state: %ld, using 100M Full Duplex\n", (long)linkState);
            speed = ETH_SPEED_100M;
            duplex = ETH_FULLDUPLEX_MODE;
            break;
        }
        
        LOCK_TCPIP_CORE();
        HAL_ETH_GetMACConfig(&heth, &MACConf);
        MACConf.DuplexMode = duplex;
        MACConf.Speed = speed;
        HAL_ETH_SetMACConfig(&heth, &MACConf);
        
        /* ETH_CODE: Manually populate and set BUF1V/OWN bits in RX descriptors
         * BEFORE starting the DMA. This ensures the hardware sees "Ready"
         * descriptors immediately upon activation. */
        printf("ETH: Manually initializing RX descriptors...\n");

        /* ETH_CODE: Reset software descriptor trackers to match hardware reset.
         * The hardware DMA will start reading from the base address (Descriptor 0).
         * We must ensure our software read pointer (RxDescIdx) matches. */
        heth.RxDescList.RxDescIdx = 0;
        heth.RxDescList.RxBuildDescIdx = 0;
        heth.RxDescList.RxBuildDescCnt = 0;

        for (uint32_t i = 0; i < ETH_RX_DESC_CNT; i++) {
          uint8_t *ptr = NULL;
          HAL_ETH_RxAllocateCallback(&ptr);
          if (ptr) {
            /* ptr already includes +2 offset from HAL_ETH_RxAllocateCallback */
            DMARxDscrTab[i].DESC0 = (uint32_t)ptr;
            /* ETH_CODE: Store buffer address in separate backup array.
             * DMA overwrites DESC0 on completion, so we need separate storage. */
            DMARxDscrBackup[i] = (uint32_t)ptr;

            /* DESC2 bitfields for H7:
             * [13:0]: Buffer 1 Length
             * [30:16]: Buffer 2 Length
             */
            DMARxDscrTab[i].DESC2 = (heth.Init.RxBuffLen & 0x3FFF);
            /* Set bits: 
             * 31 (OWN): DMA owns the descriptor
             * 30 (IOC): Interrupt On Completion (Enable this!)
             * 24 (BUF1V): Buffer 1 is valid
             * Note: IOC (Bit 30) is CRITICAL for receiving interrupts. */
            DMARxDscrTab[i].DESC3 = 0x80000000 | 0x40000000 | 0x01000000;
            
            /* ETH_CODE: Clean DCache to ensure DMA sees these values in RAM! */
            SCB_CleanDCache_by_Addr((uint32_t *)&DMARxDscrTab[i], sizeof(ETH_DMADescTypeDef_Shadow));
            
            /* Ensure memory write is complete */
            __DSB();
            
            printf("RX Init Desc %lu: addr=0x%08lx, bkup=0x%08lx, len=%lu, DESC3=0x%08lx\n",
                   (unsigned long)i, (unsigned long)DMARxDscrTab[i].DESC0,
                   (unsigned long)DMARxDscrBackup[i],
                   (unsigned long)DMARxDscrTab[i].DESC2, (unsigned long)DMARxDscrTab[i].DESC3);
            
            /* ETH_CODE: Update HAL internal tracking to match the new packet buffer availability */
            heth.RxDescList.RxBuildDescCnt++;
            heth.RxDescList.RxBuildDescIdx = (heth.RxDescList.RxBuildDescIdx + 1) % ETH_RX_DESC_CNT;
          } else {
            printf("ERROR: Failed to allocate buffer for RX descriptor %lu!\n", (unsigned long)i);
          }
        }
        printf("ETH: RX descriptors initialized successfully\n");

        if (HAL_ETH_Start_IT(&heth) != HAL_OK) {
          printf("ERROR: HAL_ETH_Start_IT failed!\n");
        } else {
          printf("HAL_ETH_Start_IT successful\n");
          
          /* ETH_CODE: internal "Kick" to DMA to poll RX descriptors immediately 
           * by updating the Tail Pointer to the end of the ring/list. */
          heth.Instance->DMACRDTPR = (uint32_t)(DMARxDscrTab + ETH_RX_DESC_CNT);
        }
        
        /* ETH_CODE: Manually enable DMA interrupts to ensure they are properly configured
         * This is needed because HAL_ETH_Start_IT() might not enable all required
         * interrupts in the RTEMS environment. */
        printf("ETH: Enabling DMA interrupts...\n");
        /* Enable RX DMA interrupts */
        __HAL_ETH_DMA_ENABLE_IT(&heth, ETH_DMA_RX_IT | ETH_DMA_NORMAL_IT);
        /* Enable TX DMA interrupts */
        __HAL_ETH_DMA_ENABLE_IT(&heth, ETH_DMA_TX_IT | ETH_DMA_NORMAL_IT);
        /* Enable MAC interrupts */
        __HAL_ETH_MAC_ENABLE_IT(&heth, ETH_MAC_RX_STATUS_IT | ETH_MAC_TX_STATUS_IT);
        printf("ETH: DMACIER = 0x%08lx, MACIER = 0x%08lx\n",
               (unsigned long)heth.Instance->DMACIER, (unsigned long)heth.Instance->MACIER);
        printf("ETH: DMACSR  = 0x%08lx\n", (unsigned long)heth.Instance->DMACSR);
        printf("ETH: SYSCFG_PMCR = 0x%08lx\n", (unsigned long)SYSCFG->PMCR);
        printf("ETH: MACCR = 0x%08lx\n", (unsigned long)heth.Instance->MACCR);
        printf("RX Desc 0: 0x%08lx 0x%08lx 0x%08lx 0x%08lx\n",
               (unsigned long)DMARxDscrTab[0].DESC0,
               (unsigned long)DMARxDscrTab[0].DESC1,
               (unsigned long)DMARxDscrTab[0].DESC2,
               (unsigned long)DMARxDscrTab[0].DESC3);
        
        /* ETH_CODE: Temporarily enable Promiscuous mode and Receive All
         * to ensure we are not missing packets due to address filtering. */
        heth.Instance->MACPFR |= ETH_MACPFR_RA | ETH_MACPFR_PR;
        
        /* ETH_CODE: Explicitly disable advanced features that could trigger Context Descriptors
         * (Timestamps, VLANs) to ensure the DMA only produces Normal descriptors. */
        /* Disable Timestamp completely - must clear bit 0 to prevent context descriptors */
        heth.Instance->MACTSCR = 0; /* Clear ALL timestamp settings to prevent CTXT descriptors */
        heth.Instance->MACVTR = 0;  /* Disable VLAN tagging */

        printf("ETH: MACCR = 0x%08lx, MACPFR = 0x%08lx, MACTSCR = 0x%08lx\n", 
               (unsigned long)heth.Instance->MACCR, 
               (unsigned long)heth.Instance->MACPFR,
               (unsigned long)heth.Instance->MACTSCR);

        netif_set_up(netif);
        netif_set_link_up(netif);
        UNLOCK_TCPIP_CORE();
        printf("Ethernet link is UP\n");
      }
    } else {
      /* Link is Down according to hardware */
      if (netif_is_link_up(netif)) {
        printf("PHY: Link Down detected (BSR=0x%04lx)\n", (unsigned long)bsr);
        LOCK_TCPIP_CORE();
        HAL_ETH_Stop_IT(&heth);
        netif_set_link_down(netif);
        netif_set_down(netif);
        UNLOCK_TCPIP_CORE();
        printf("Ethernet link is DOWN\n");
      }
    }

    sys_msleep(500);
  }
}

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
/* USER CODE BEGIN HAL ETH RxAllocateCallback */

  printf("\n========== RX_ALLOCATE_CALLBACK ==========\n");
  struct pbuf_custom *p = LWIP_MEMPOOL_ALLOC(RX_POOL);
  if (p)
  {
    printf("RX_ALLOC: Allocated pbuf_custom at 0x%p\n", p);
    /* Get the buff from the struct pbuf address. */
    /* ETH_CODE: With ETH_PAD_SIZE=2, DMA receives data at buff[2] so that IP header
     * is 4-byte aligned. The pbuf points to the actual buffer, but we report
     * buff+2 to the HAL so that DMA writes start at the correct offset. */
    *buff = (uint8_t *)p + offsetof(RxBuff_t, buff) + 2;
    printf("RX_ALLOC: buff=0x%p (with +2 offset), actual buff=0x%p\n", *buff, (uint8_t *)p + offsetof(RxBuff_t, buff));
    p->custom_free_function = pbuf_free_custom;
    /* Initialize the struct pbuf.
    * This must be performed whenever a buffer's allocated because it may be
    * changed by lwIP or the app, e.g., pbuf_free decrements ref. */
    /* ETH_CODE: Pass actual buffer start (without +2) so pbuf knows the true memory location */
    pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, p, (uint8_t *)p + offsetof(RxBuff_t, buff), ETH_RX_BUFFER_SIZE);
    printf("RX_ALLOC: pbuf initialized successfully\n");
  }
  else
  {
    printf("RX_ALLOC: FAILED - RX_POOL exhausted!\n");
    RxAllocStatus = RX_ALLOC_ERROR;
    *buff = NULL;
  }
  printf("========== RX_ALLOCATE_CALLBACK END ==========\n\n");
/* USER CODE END HAL ETH RxAllocateCallback */
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
/* USER CODE BEGIN HAL ETH RxLinkCallback */

  struct pbuf **ppStart = (struct pbuf **)pStart;
  struct pbuf **ppEnd = (struct pbuf **)pEnd;
  struct pbuf *p = NULL;

  /* ETH_CODE: buff points to buff+2 (due to ETH_PAD_SIZE=2 offset in HAL_ETH_RxAllocateCallback)
   * Calculate actual buffer start and adjust pbuf accordingly. */
  uint8_t *actual_buff = buff - 2;
  
  /* Get the struct pbuf from the actual buff address. */
  p = (struct pbuf *)(actual_buff - offsetof(RxBuff_t, buff));
  p->next = NULL;
  p->tot_len = 0;
  p->len = Length;

  /* Chain the buffer. */
  if (!*ppStart)
  {
    /* The first buffer of the packet. */
    *ppStart = p;
  }
  else
  {
    /* Chain the buffer to the end of the packet. */
    (*ppEnd)->next = p;
  }
  *ppEnd  = p;

  /* Update the total length of all the buffers of the chain. Each pbuf in the chain should have its tot_len
   * set to its own length, plus the length of all the following pbufs in the chain. */
  for (p = *ppStart; p != NULL; p = p->next)
  {
    p->tot_len += Length;
  }

  /* ETH_CODE: Adjust pbuf payload to point to the offset region (buff+2)
   * where the Ethernet header actually resides. */
  p->payload = (uint8_t *)p->payload + 2;

  /* ETH_CODE: Invalidate cache for the actual data region starting at actual_buff. */
  SCB_InvalidateDCache_by_Addr((uint32_t *)actual_buff, Length);

/* USER CODE END HAL ETH RxLinkCallback */
}

void HAL_ETH_TxFreeCallback(uint32_t * buff)
{
/* USER CODE BEGIN HAL ETH TxFreeCallback */

  printf("\n========== TX_FREE_CALLBACK ==========\n");
  printf("TX_FREE: buff=0x%p\n", buff);
  pbuf_free((struct pbuf *)buff);
  printf("TX_FREE: pbuf freed\n");
  printf("========== TX_FREE_CALLBACK END ==========\n\n");

/* USER CODE END HAL ETH TxFreeCallback */
}

/* USER CODE BEGIN 8 */
/* ETH_CODE: add functions needed for proper multithreading support and check */

/* CMSIS-OS specific locking checks removed for RTEMS port */
void sys_lock_tcpip_core(void){
  LOCK_TCPIP_CORE();
}

void sys_unlock_tcpip_core(void){
  UNLOCK_TCPIP_CORE();
}

void sys_check_core_locking(void){
  /* Not implemented for RTEMS port */
}

void sys_mark_tcpip_thread(void){
  /* Not implemented for RTEMS port */
}

static void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();
 
  printf("MPU_Config: CCR before = 0x%08lx\n", (unsigned long)SCB->CCR);
  SCB->CCR &= ~SCB_CCR_UNALIGN_TRP_Msk;
  __DSB();
  __ISB();
  printf("MPU_Config: CCR after  = 0x%08lx\n", (unsigned long)SCB->CCR);
 
  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Region 1: D2 SRAM (Buffers & Descriptors) - Non-cacheable, Non-bufferable
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x30000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1; /* Normal property */
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
/* USER CODE END 8 */
