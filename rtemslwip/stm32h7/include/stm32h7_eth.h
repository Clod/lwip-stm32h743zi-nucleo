/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * File Name          : stm32h7_eth.h
  * Description        : This file provides initialization code for LWIP
  *                      middleWare.
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

#ifndef __STM32H7_ETH_H__
#define __STM32H7_ETH_H__

#include <stm32h7xx_hal.h>
#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/sys.h"

/* Within 'USER CODE' section, code will be kept by default at each generation */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* Exported functions ------------------------------------------------------- */
err_t ethernetif_init(struct netif *netif);

void ethernetif_input(void* argument);
void ethernet_link_thread(void* argument );

void Error_Handler(void);

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
#endif
