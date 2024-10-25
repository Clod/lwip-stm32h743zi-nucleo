/*
 * Copyright (C) 2022 On-Line Applications Research Corporation (OAR)
 * Written by Kinsey Moore <kinsey.moore@oarcorp.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <rtems/rtems/cache.h>
#include <rtems/rtems/intr.h>
#include <rtems/score/threadimpl.h>
#include <libcpu/mmu-vmsav8-64.h>
#include <stdio.h>
#include <string.h>
#include <xil-compat-lwip.h>

#define TWO_MB (2*1024*1024)
#define ONE_GB (1024*1024*1024)

/*
 * When altering memory attributes, Xilinx sets them for differing memory sizes
 * depending on what area of memory they are in. Any attribute changes below 4GB
 * apply to 2MB chunks while any changes above 4GB apply to 1GB chunks.
 */
void Xil_SetTlbAttributes( UINTPTR Addr, u64 attrib )
{
  rtems_status_code sc;
  sc = aarch64_mmu_map(
    Addr,
    Addr < 0x100000000 ? TWO_MB : ONE_GB,
    attrib
  );
  if ( sc == RTEMS_NO_MEMORY ) {
    printf("Out of memory setting MMU attributes on ptr %p: 0x%lx\n", (void*)Addr, attrib);
  } else if ( sc == RTEMS_INVALID_ADDRESS ) {
    printf("Attempted to set MMU attributes on invalid ptr %p: 0x%lx\n", (void*)Addr, attrib);
  } else if ( sc != RTEMS_SUCCESSFUL ) {
    printf("Failed setting MMU attributes on ptr %p: 0x%lx\n", (void*)Addr, attrib);
  }
}

#include "FreeRTOS.h"

/*
 * XInterruptHandler function pointer signature just happens to exactly match
 * rtems_interrupt_handler
 */
BaseType_t xPortInstallInterruptHandler(
  uint8_t           ucInterruptID,
  XInterruptHandler pxHandler,
  void             *pvCallBackRef
)
{
  char name[10];

  /* Is this running in the context of any interrupt server tasks? */
  _Thread_Get_name( _Thread_Get_executing(), name, sizeof( name ) );
  if (strcmp(name, "IRQS") == 0) {
    /* Can't run this from within an IRQ Server thread context */
    return RTEMS_ILLEGAL_ON_SELF;
  }

  rtems_status_code sc = rtems_interrupt_server_handler_install(
    RTEMS_INTERRUPT_SERVER_DEFAULT,
    ucInterruptID,
    "CGEM Handler",
    RTEMS_INTERRUPT_UNIQUE,
    pxHandler,
    pvCallBackRef
  );

  return sc;
}

/* Enable the interrupt */
void XScuGic_EnableIntr ( u32 DistBaseAddress, u32 Int_Id )
{
  rtems_interrupt_vector_enable( Int_Id );
}

/* Disable the interrupt */
void XScuGic_DisableIntr ( u32 DistBaseAddress, u32 Int_Id )
{
  rtems_interrupt_vector_disable( Int_Id );
}
