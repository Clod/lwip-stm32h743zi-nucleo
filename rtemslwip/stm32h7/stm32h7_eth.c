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

typedef struct
{
  struct pbuf_custom pbuf_custom;
  uint8_t buff[(ETH_RX_BUFFER_SIZE + 31) & ~31] __ALIGNED(32);
} RxBuff_t;

/* Memory Pool Manual Declaration (points to D2 SRAM) */
#define ETH_RX_BUFFER_CNT             12U
#define RX_POOL_BASE_ADDR             0x30040200

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

/* ETH_CODE: The system HAL header defines ETH_DMADescTypeDef as 24 bytes, 
 * but the H7 hardware REQUIRES 32-byte (8-word) alignment/spacing for 
 * descriptors in enhanced mode. We define a shadow struct to force 32-byte stride. */
typedef struct
{
  __IO uint32_t DESC0;
  __IO uint32_t DESC1;
  __IO uint32_t DESC2;
  __IO uint32_t DESC3;
  uint32_t BackupAddr0;
  uint32_t BackupAddr1;
  uint32_t Reserved[2];
} ETH_DMADescTypeDef_Shadow;

__IO uint32_t TxPkt = 0;
__IO uint32_t RxPkt = 0;

ETH_DMADescTypeDef_Shadow *DMARxDscrTab = (ETH_DMADescTypeDef_Shadow *)0x30040000; /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef_Shadow *DMATxDscrTab = (ETH_DMADescTypeDef_Shadow *)0x30040080; /* Ethernet Tx DMA Descriptors */

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

static void stm32h7_eth_interrupt_handler(void *arg)
{
  EthIrqCount++;
  /* Debug: Log interrupt entry */
  printf("ETH IRQ: EthIrqCount=%lu\n", (unsigned long)EthIrqCount);
  HAL_ETH_IRQHandler(&heth);
}

/**
  * @brief  Ethernet Rx Transfer completed callback
  * @param  handlerEth: ETH handler
  * @retval None
  */
void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *handlerEth)
{
  RxIrqCount++;
  printf("ETH RX Callback: RxIrqCount=%lu\n", (unsigned long)RxIrqCount);
  sys_sem_signal(&RxPktSemaphore);
}
/**
  * @brief  Ethernet Tx Transfer completed callback
  * @param  handlerEth: ETH handler
  * @retval None
  */
void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *handlerEth)
{
  sys_sem_signal(&TxPktSemaphore);
}
/**
  * @brief  Ethernet DMA transfer error callback
  * @param  handlerEth: ETH handler
  * @retval None
  */
void HAL_ETH_ErrorCallback(ETH_HandleTypeDef *handlerEth)
{
  uint32_t dma_err = HAL_ETH_GetDMAError(handlerEth);
  printf("ETH Error Callback: DMA Error=0x%08lx\n", (unsigned long)dma_err);
  
  /* Diagnostic: Dump descriptor and HAL state on error */
  printf("ETH Error: RxIdx=%lu, RxBuildIdx=%lu, RxBuildCnt=%lu\n",
         (unsigned long)handlerEth->RxDescList.RxDescIdx,
         (unsigned long)handlerEth->RxDescList.RxBuildDescIdx,
         (unsigned long)handlerEth->RxDescList.RxBuildDescCnt);
  
  for(int i=0; i<ETH_RX_DESC_CNT; i++) {
    ETH_DMADescTypeDef_Shadow *d = &DMARxDscrTab[i];
    printf("RX Desc %d [0x%08lx]: 0x%08lx 0x%08lx 0x%08lx 0x%08lx\n", i,
           (unsigned long)d,
           (unsigned long)d->DESC0,
           (unsigned long)d->DESC1,
           (unsigned long)d->DESC2,
           (unsigned long)d->DESC3);
  }

  if((dma_err & ETH_DMACSR_RBU) == ETH_DMACSR_RBU)
  {
     printf("ETH Error: Receive Buffer Unavailable\n");
     
     /* ETH_CODE: Do NOT rebuild descriptors here. 
      * It causes a race condition where valid packets in the buffer 
      * are overwritten before the application thread reads them.
      * Just signal the thread to run.
      */
     // ethernetif_rebuild_rx_descriptors();
     
     /* Clear the RBU bit in the DMA status register */
     __HAL_ETH_DMA_CLEAR_FLAG(handlerEth, ETH_DMACSR_RBU);
     
     /* Signal the input task to process any pending packets */
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
 heth.Init.TxDesc = (ETH_DMADescTypeDef *)DMATxDscrTab;
 heth.Init.RxDesc = (ETH_DMADescTypeDef *)DMARxDscrTab;
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
    heth.RxDescList.RxDescCnt = ETH_RX_DESC_CNT;
    heth.RxDescList.RxDescIdx = 0;
    heth.RxDescList.RxBuildDescIdx = 0;
    heth.RxDescList.RxBuildDescCnt = 0; /* Will be incremented during buffer allocation */
    heth.RxDescList.pRxStart = &DMARxDscrTab[0];
    heth.RxDescList.pRxEnd = &DMARxDscrTab[ETH_RX_DESC_CNT-1];

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
  if (sys_sem_new(&RxPktSemaphore, 1) != ERR_OK) {
        /* Handle error */
        printf("ERROR: Failed to create RxPktSemaphore\n");
  }

  printf("Creating TxPktSemaphore...\n");
  /* create a binary semaphore used for informing ethernetif of frame transmission */
  if (sys_sem_new(&TxPktSemaphore, 1) != ERR_OK) {
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
  uint32_t i = 0U;
  struct pbuf *q = NULL;
  err_t errval = ERR_OK;
  ETH_BufferTypeDef Txbuffer[ETH_TX_DESC_CNT];

  memset(Txbuffer, 0 , ETH_TX_DESC_CNT*sizeof(ETH_BufferTypeDef));

  for(q = p; q != NULL; q = q->next)
  {
    if(i >= ETH_TX_DESC_CNT)
      return ERR_IF;

    Txbuffer[i].buffer = q->payload;
    Txbuffer[i].len = q->len;

    if(i>0)
    {
      Txbuffer[i-1].next = &Txbuffer[i];
    }

    if(q->next == NULL)
    {
      Txbuffer[i].next = NULL;
    }

    i++;
  }

  TxConfig.Length = p->tot_len;
  TxConfig.TxBuffer = Txbuffer;
  TxConfig.pData = p;

  pbuf_ref(p);

  for(q = p; q != NULL; q = q->next)
  {
    SCB_CleanDCache_by_Addr((uint32_t *)q->payload, q->len);
  }

  printf("TX: %u bytes\n", (unsigned int)p->tot_len);

  HAL_ETH_Transmit_IT(&heth, &TxConfig);
  sys_arch_sem_wait(&TxPktSemaphore, TIME_WAITING_FOR_INPUT);

  HAL_ETH_ReleaseTxPacket(&heth);

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
static void ethernetif_rebuild_rx_descriptors(void)
{
  uint32_t idx = heth.RxDescList.RxDescIdx;
  
  /* Rebuild descriptors that have been processed (OWN=0) */
  while ((DMARxDscrTab[idx].DESC3 & 0x80000000) == 0) {
    uint8_t *ptr = NULL;
    
    /* Allocate a new buffer for this descriptor */
    HAL_ETH_RxAllocateCallback(&ptr);
    
    if (ptr != NULL) {
      /* Set the new buffer address */
      DMARxDscrTab[idx].DESC0 = (uint32_t)ptr;
      DMARxDscrTab[idx].BackupAddr0 = (uint32_t)ptr;
      
      /* Set buffer length */
      DMARxDscrTab[idx].DESC2 = (heth.Init.RxBuffLen & 0x3FFF);
      
      /* Return descriptor to DMA: set OWN and BUF1V bits */
      DMARxDscrTab[idx].DESC3 = 0x80000000 | 0x01000000;
      
      /* Ensure memory write is visible to DMA */
      __DSB();
      
      /* Update HAL tracking */
      heth.RxDescList.RxBuildDescCnt++;
      heth.RxDescList.RxBuildDescIdx = (idx + 1) % ETH_RX_DESC_CNT;
      
      printf("Rebuilt RX desc %lu: addr=0x%08lx, DESC3=0x%08lx\n",
             (unsigned long)idx, (unsigned long)DMARxDscrTab[idx].DESC0,
             (unsigned long)DMARxDscrTab[idx].DESC3);
    } else {
      printf("ERROR: Failed to allocate buffer for RX descriptor rebuild %lu\n", (unsigned long)idx);
      RxAllocStatus = RX_ALLOC_ERROR;
      break;
    }
    
    /* Move to next descriptor */
    idx = (idx + 1) % ETH_RX_DESC_CNT;
    
    /* Safety: don't loop forever if all descriptors are already owned by DMA */
    if (idx == heth.RxDescList.RxDescIdx) {
      break;
    }
  }
}

static struct pbuf * low_level_input(struct netif *netif)
{
  struct pbuf *p = NULL;

  printf("low_level_input: RxAllocStatus=%d\n", RxAllocStatus);
  if(RxAllocStatus == RX_ALLOC_OK)
  {
    HAL_StatusTypeDef status = HAL_ETH_ReadData(&heth, (void **)&p);
    printf("low_level_input: HAL_ETH_ReadData status=%d, got pbuf 0x%p\n", (int)status, p);
    
    if (status != HAL_OK || p == NULL) {
        /* Extra diagnostic on failure */
        printf("low_level_input: HAL_ETH_ReadData FAILED (stat=%d)! Trying manual read...\n", (int)status);
        
        /* ETH_CODE: Manual Packet Read Fallback 
         * Check if the current descriptor is owned by CPU (Bit 31 = 0) */
        uint32_t idx = heth.RxDescList.RxDescIdx;
        ETH_DMADescTypeDef_Shadow *d = &DMARxDscrTab[idx];
        
        printf("Manual Check: Idx=%lu, DESC0=0x%08lx, DESC3=0x%08lx\n", 
               (unsigned long)idx, (unsigned long)d->DESC0, (unsigned long)d->DESC3);
        
        if ((d->DESC3 & 0x80000000) == 0 && (d->DESC3 & 0x20000000) && (d->DESC3 & 0x10000000)) {
             /* CPU owned + First Desc + Last Desc (Single packet) */
             /* Remove unused len variable to fix warning */
             
             /* Re-reading H7 Reference Manual for RDES3 Write-Back:
              * Bit 31: OWN
              * Bit 30: CTXT
              * Bit 29: FD
              * Bit 28: LD
              * Bits 14:0: Packet Length (PL)
              */
             uint32_t pkt_len = (d->DESC3 & 0x00007FFF);
             
             printf("Manual Read: Idx=%lu, Addr=0x%08lx, Len=%lu\n", (unsigned long)idx, (unsigned long)d->BackupAddr0, (unsigned long)pkt_len);
             
             /* Get the pbuf from the back-up address */
             if (d->BackupAddr0 != 0) {
                 uint8_t *buff = (uint8_t *)d->BackupAddr0;
                 struct pbuf *p_manual = (struct pbuf *)(buff - offsetof(RxBuff_t, buff));
                 p_manual->next = NULL;
                 p_manual->tot_len = pkt_len;
                 p_manual->len = pkt_len;
                 
                 /* Invalidate Cache */
                 SCB_InvalidateDCache_by_Addr((uint32_t *)buff, pkt_len);
                 
                 p = p_manual;
                 
                 /* Update HAL tracking manually since we bypassed ReadData */
                 heth.RxDescList.RxDescIdx = (idx + 1) % ETH_RX_DESC_CNT;
                 
                 /* Rebuild descriptors immediately */
                 ethernetif_rebuild_rx_descriptors();
             }
        } else {
             printf("Manual Read: Desc not ready or weird state. DESC3=0x%08lx\n", (unsigned long)d->DESC3);
        }
    } else {
        /* ETH_CODE: After successfully reading data, rebuild RX descriptors
         * to return them to the DMA. This prevents "Receive Buffer Unavailable" errors. */
        ethernetif_rebuild_rx_descriptors();
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
      do
      {
        p = low_level_input( netif );
        if (p != NULL)
        {
          printf("RX: %u bytes\n", (unsigned int)p->tot_len);
          if (netif->input( p, netif) != ERR_OK )
          {
            pbuf_free(p);
          }
        }
      } while(p!=NULL);
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
  printf("pbuf_free_custom: p=0x%p\n", p);
  struct pbuf_custom* custom_pbuf = (struct pbuf_custom*)p;
  LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf);

  /* If the Rx Buffer Pool was exhausted, signal the ethernetif_input task to
   * call HAL_ETH_GetRxDataBuffer to rebuild the Rx descriptors. */

  if (RxAllocStatus == RX_ALLOC_ERROR)
  {
    RxAllocStatus = RX_ALLOC_OK;
    sys_sem_signal(&RxPktSemaphore);
  }
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
        for (uint32_t i = 0; i < ETH_RX_DESC_CNT; i++) {
          uint8_t *ptr = NULL;
          HAL_ETH_RxAllocateCallback(&ptr);
          if (ptr) {
            DMARxDscrTab[i].DESC0 = (uint32_t)ptr;
            /* ETH_CODE: Store buffer address in BackupAddr0. 
             * ALL STM32 HAL versions use this to retrieve the buffer address 
             * independently of what the DMA writes back to DESC0. */
            DMARxDscrTab[i].BackupAddr0 = (uint32_t)ptr;

            /* DESC2 bitfields for H7:
             * [13:0]: Buffer 1 Length
             * [30:16]: Buffer 2 Length
             */
            DMARxDscrTab[i].DESC2 = (heth.Init.RxBuffLen & 0x3FFF);
            /* Set bits: 
             * 31 (OWN): DMA owns the descriptor
             * 24 (BUF1V): Buffer 1 is valid
             * Note: IOC (Bit 30) removed to avoid potential Context Descriptor 
             * confusion during write-back in some hardware revisions. */
            DMARxDscrTab[i].DESC3 = 0x80000000 | 0x01000000;
            
            /* Ensure memory write is complete */
            __DSB();
            
            printf("RX Init Desc %lu: addr=0x%08lx, bkup=0x%08lx, len=%lu, DESC3=0x%08lx\n",
                   (unsigned long)i, (unsigned long)DMARxDscrTab[i].DESC0, 
                   (unsigned long)DMARxDscrTab[i].BackupAddr0,
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
          heth.Instance->DMACRDTPR = (uint32_t)heth.RxDescList.pRxEnd;
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
        heth.Instance->MACTSCR &= ~(0x00000001); /* Disable Timestamp */
        heth.Instance->MACVTR = 0;              /* Disable VLAN tagging (Correct register name: MACVTR) */

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

  struct pbuf_custom *p = LWIP_MEMPOOL_ALLOC(RX_POOL);
  if (p)
  {
    /* Get the buff from the struct pbuf address. */
    *buff = (uint8_t *)p + offsetof(RxBuff_t, buff);
    p->custom_free_function = pbuf_free_custom;
    /* Initialize the struct pbuf.
    * This must be performed whenever a buffer's allocated because it may be
    * changed by lwIP or the app, e.g., pbuf_free decrements ref. */
    pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, p, *buff, ETH_RX_BUFFER_SIZE);
  }
  else
  {
    RxAllocStatus = RX_ALLOC_ERROR;
    *buff = NULL;
  }
/* USER CODE END HAL ETH RxAllocateCallback */
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
/* USER CODE BEGIN HAL ETH RxLinkCallback */

  struct pbuf **ppStart = (struct pbuf **)pStart;
  struct pbuf **ppEnd = (struct pbuf **)pEnd;
  struct pbuf *p = NULL;

  /* Get the struct pbuf from the buff address. */
  p = (struct pbuf *)(buff - offsetof(RxBuff_t, buff));
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

  /* Invalidate data cache because Rx DMA's writing to physical memory makes it stale. */
  SCB_InvalidateDCache_by_Addr((uint32_t *)buff, Length);

/* USER CODE END HAL ETH RxLinkCallback */
}

void HAL_ETH_TxFreeCallback(uint32_t * buff)
{
/* USER CODE BEGIN HAL ETH TxFreeCallback */

  pbuf_free((struct pbuf *)buff);

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

