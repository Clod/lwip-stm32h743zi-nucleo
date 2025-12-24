# Walkthrough - STM32H743ZI Nucleo Support

## Changes Implemented

### Board Support Package (BSP) Definition
- Created `defs/bsps/arm/nucleo-h743zi.json` to map the RTEMS BSP to the driver files.
- **Note**: The BSP name `nucleo-h743zi` matches the installed RTEMS BSP found in `/opt/rtems/6.1/lib/pkgconfig`.

### Driver Porting
- **Ethernet Driver**:
  - Imported `ethernetif.c` as `rtemslwip/stm32h7/stm32h7_eth.c`.
  - Replaced CMSIS-RTOS calls (`osDelay` -> `sys_msleep`, `osSemaphore*` -> `sys_sem*`, `osThread*` -> `sys_thread_new`).
  - Removed unused CMSIS types (`osThreadAttr_t`) and internal locking checks.
  - Adapted `stm32h7_eth.h` from `ethernetif.h`.
- **PHY Driver**:
  - Imported `lan8742.c` as `rtemslwip/stm32h7/stm32h7_lan8742.c`.
  - Created `rtemslwip/stm32h7/include/stm32h7_lan8742.h` from `lan8742.h`.
- **Configuration**:
  - Created `rtemslwip/stm32h7/include/lwipbspopts.h` to define board-specific options like `ETH_RX_BUFFER_SIZE` (set to 1536).

## Verification
### Build Verification
The build was successfully verified with the following commands:

1.  **Configure**:
    ```bash
    ./waf configure --rtems-bsps=arm/nucleo-h743zi --rtems=/opt/rtems/6.1 --rtems-version=6
    ```
    *Output*: `'configure' finished successfully`

2.  **Build**:
    ```bash
    ./waf
    ```
    *Output*: `'build-arm-rtems6-nucleo-h743zi' finished successfully`

### Drivers Compiled
- `rtemslwip/stm32h7/stm32h7_lan8742.c`
- `rtemslwip/stm32h7/stm32h7_eth.c`
- `rtemslwip/test/networking01/sample_app.c` (Test application)

## Usage
To use this BSP in your application, ensure you configure `waf` with `--rtems-bsps=arm/nucleo-h743zi`.
