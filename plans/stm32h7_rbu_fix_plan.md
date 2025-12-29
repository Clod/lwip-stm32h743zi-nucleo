# STM32H7 Ethernet RBU Loop Fix Plan

## Problem Summary
The STM32H7 Ethernet DMA enters an infinite **Receive Buffer Unavailable (RBU)** loop because:
1. The tail pointer calculation tells DMA there are no available descriptors
2. The descriptor structure size doesn't match hardware requirements

## Root Causes

### 1. Incorrect Tail Pointer Calculation
**File**: [`stm32h7_eth.c`](rtemslwip/stm32h7/stm32h7_eth.c:185)
**Function**: `stm32h7_eth_kick_rx_dma()`

```c
// CURRENT (WRONG):
uint32_t tail_idx = (read_idx == 0) ? (ETH_RX_DESC_CNT - 1) : (read_idx - 1);
heth.Instance->DMACRDTPR = (uint32_t)&DMARxDscrTab[tail_idx];
```

**Problem**: When `ReadIdx=1`, `TailIdx=0`. DMA sees descriptors 0-2 as "processed" and stops. RBU triggers immediately.

**Fix**: Point tail to the LAST descriptor in the ring:
```c
// FIXED:
heth.Instance->DMACRDTPR = (uint32_t)&DMARxDscrTab[ETH_RX_DESC_CNT - 1];
```

### 2. Descriptor Structure Size Mismatch
**File**: [`stm32h7_eth.c`](rtemslwip/stm32h7/stm32h7_eth.c:131)
**Struct**: `ETH_DMADescTypeDef_Shadow`

```c
// CURRENT (32 bytes - WRONG for H7):
typedef struct
{
  __IO uint32_t DESC0;  // 0x00
  __IO uint32_t DESC1;  // 0x04
  __IO uint32_t DESC2;  // 0x08
  __IO uint32_t DESC3;  // 0x0C
  uint32_t BackupAddr0; // 0x10 - EXTRA
  uint32_t BackupAddr1; // 0x14 - EXTRA
  uint32_t Reserved[2]; // 0x18-0x1C - EXTRA
} ETH_DMADescTypeDef_Shadow;
```

**Problem**: STM32H7 DMA expects exactly 16 bytes per descriptor. The extra 16 bytes corrupt ring layout.

**Fix**: Use packed 16-byte structure:
```c
typedef struct __attribute__((packed))
{
  __IO uint32_t DESC0;
  __IO uint32_t DESC1;
  __IO uint32_t DESC2;
  __IO uint32_t DESC3;
} ETH_DMADescTypeDef_Shadow;
```

### 3. Ring Length Register Configuration
**File**: [`stm32h7_eth.c`](rtemslwip/stm32h7/stm32h7_eth.c:394)

The `DMACRDRLR` should be set to `(ETH_RX_DESC_CNT - 1)` which is correct (3 for 4 descriptors). This represents the **number of descriptors minus 1**.

## Required Changes

### Change 1: Fix Descriptor Structure
```c
// Replace lines 131-140 with:
typedef struct __attribute__((packed))
{
  __IO uint32_t DESC0;
  __IO uint32_t DESC1;
  __IO uint32_t DESC2;
  __IO uint32_t DESC3;
} ETH_DMADescTypeDef_Shadow;
```

### Change 2: Fix Tail Pointer Function
```c
// Replace lines 185-202 with:
static void stm32h7_eth_kick_rx_dma(void)
{
    // Tail pointer must ALWAYS point to the last descriptor in the ring
    // This tells DMA "descriptors 0 to N-1 are available"
    heth.Instance->DMACRDTPR = (uint32_t)&DMARxDscrTab[ETH_RX_DESC_CNT - 1];
    __DSB();
    
    // Write Poll Demand to force immediate re-fetch
    *(__IO uint32_t *)((uint32_t)heth.Instance + 0x104C) = 0;
}
```

### Change 3: Remove BackupAddr References
The code uses `BackupAddr0` to store buffer addresses. This needs to be moved to a separate array or handled differently since it's being removed from the descriptor structure.

**Option A**: Create separate backup array:
```c
static uint32_t DMARxDscrBackup[ETH_RX_DESC_CNT];
```

**Option B**: Use DESC0 directly (DMA doesn't overwrite until OWN=0):
```c
// DMA only writes back when OWN=0, so DESC0 still valid
```

## Verification Checklist

After applying fixes:
- [ ] Descriptor structure is exactly 16 bytes
- [ ] All descriptors initialized with OWN=1, IOC=1, BUF1V=1
- [ ] Tail pointer always points to last descriptor (index 3)
- [ ] DMA processes packets without RBU loop
- [ ] Packets successfully received and passed to LwIP stack
- [ ] No descriptor corruption observed in debug output

## Alternative Approach: Use HAL Descriptor Type

Instead of custom shadow struct, use the HAL's `ETH_DMADescTypeDef` directly and ensure proper alignment:
```c
ETH_DMADescTypeDef *DMARxDscrTab = (ETH_DMADescTypeDef *)0x30000000;
```
Ensure 32-byte alignment in linker script:
```ld
.eth_rx_desc 0x30000000 (NOLOAD) : { *(.eth_rx_desc*) } > SRAM1
```
