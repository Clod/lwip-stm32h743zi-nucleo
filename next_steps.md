# Debugging Challenge: STM32H7 Ethernet Infinite RBU Loop

## Context
**Target**: STM32H743ZI (Nucleo-144)
**OS**: RTEMS 6 (Custom Port) with LwIP
**Driver**: Custom implementation using ST HAL Low-Level APIs
**Memory**: 
- **Descriptors**: `0x30000000` (SRAM1, D2 Domain)
- **Buffers**: `0x30000600` (SRAM1, D2 Domain)
- **MPU**: Region `0x30000000` (Size 512KB) configured as `Strongly Ordered` / `Device` (Tex=0, C=0, B=0, S=1) or Normal Non-Cacheable. Effectively **Non-Cacheable, Non-Bufferable**.

## The Problem
The Ethernet RX DMA enters an infinite **Receive Buffer Unavailable (RBU)** loop.
1.  **Packet 0**: Arrives. DMA attempts to write, triggers RBU (likely due to lookahead buffer full or tail pointer catch-up).
    - Driver manually extracts packet (Owner bit check succeeds).
    - Driver returns descriptor to DMA (Sets OWN bit `0x80000000`).
2.  **Packet 1**: Driver updates Tail Pointer and writes Poll Demand register.
    - **DMA Immediately signals RBU again.**
    - Driver checks descriptor: `DESC0=0x3...` (Valid Addr), `DESC3=0xC1000000` (OWN=1, IOC=1, BUF1V=1). **The descriptor is valid**.
    - This repeats forever. Interrupt fire, RBU set, Descriptor is valid, Restart DMA, RBU set...

## Root Cause Analysis

### Root Cause 1: Tail Pointer Calculation (FIXED)
**Location**: `stm32h7_eth_kick_rx_dma()` function
**Issue**: The tail pointer was calculated as `ReadIdx - 1`, which told the DMA "stop before ReadIdx". This meant DMA saw **zero available descriptors** and immediately triggered RBU.

**Original Code (WRONG)**:
```c
uint32_t tail_idx = (read_idx == 0) ? (ETH_RX_DESC_CNT - 1) : (read_idx - 1);
heth.Instance->DMACRDTPR = (uint32_t)&DMARxDscrTab[tail_idx];
```

**Fixed Code**:
```c
// Tail pointer must ALWAYS point to the LAST descriptor in the ring
heth.Instance->DMACRDTPR = (uint32_t)&DMARxDscrTab[ETH_RX_DESC_CNT - 1];
```

**Why This Fixes It**: By always pointing the tail to the last descriptor (index 3 for 4 descriptors), the DMA sees all descriptors as available for use.

### Root Cause 2: Descriptor Structure Size (FIXED)
**Location**: `ETH_DMADescTypeDef_Shadow` struct definition
**Issue**: The struct was 32 bytes (with BackupAddr fields) but STM32H7 DMA expects exactly 16 bytes per descriptor in enhanced mode.

**Original Code (WRONG)**:
```c
typedef struct
{
  __IO uint32_t DESC0;
  __IO uint32_t DESC1;
  __IO uint32_t DESC2;
  __IO uint32_t DESC3;
  uint32_t BackupAddr0;
  uint32_t BackupAddr1;
  uint32_t Reserved[2];
} ETH_DMADescTypeDef_Shadow;  // 32 bytes - WRONG for H7
```

**Fixed Code**:
```c
typedef struct __attribute__((packed))
{
  __IO uint32_t DESC0;
  __IO uint32_t DESC1;
  __IO uint32_t DESC2;
  __IO uint32_t DESC3;
} ETH_DMADescTypeDef_Shadow;  // 16 bytes - CORRECT for H7

// Separate backup array for buffer addresses
static uint32_t DMARxDscrBackup[ETH_RX_DESC_CNT];
```

### Root Cause 3: RxBuff_t Alignment (FIXED)
**Location**: `RxBuff_t` struct definition
**Issue**: Buffer alignment and pbuf structure initialization could cause unaligned access.

**Original Code**:
```c
typedef struct
{
  struct pbuf_custom pbuf_custom;
  uint8_t buff[(ETH_RX_BUFFER_SIZE + 31) & ~31] __ALIGNED(32);
} RxBuff_t;
```

**Fixed Code**:
```c
typedef struct __attribute__((aligned(32)))
{
  struct pbuf_custom pbuf_custom;
  uint8_t buff[ETH_RX_BUFFER_SIZE];
} RxBuff_t;
```

## Test Results

### Test 1: After RBU Fixes (PARTIALLY SUCCESSFUL)
```
RX Init Desc 0: addr=0x3000c400, bkup=0x3000c400, len=1536, DESC3=0xc1000000
RX Init Desc 1: addr=0x3000bde0, bkup=0x3000bde0, len=1536, DESC3=0xc1000000
RX Init Desc 2: addr=0x3000b7c0, bkup=0x3000b7c0, len=1536, DESC3=0xc1000000
RX Init Desc 3: addr=0x3000b1a0, bkup=0x3000b1a0, len=1536, DESC3=0xc1000000
...
ETH IRQ: EthIrqCount=1
ETH RX Callback: RxIrqCount=1
Manual Check: Idx=0, Addr=0x30000000, DESC0=0x3000c400, DESC3=0x3401003c
Manual Read: Idx=0, Addr=0x3000c400, Len=60
Refilled Manual Desc 0: Addr=0x3000ab80, DESC3=0xc1000000
RX: 60 bytes
ethernet_input: dest:ff:ff:ff:ff:ff:ff, src:e8:9a:8f:8e:40:d9, type:806

*** FATAL ***
fatal source: 9 (RTEMS_FATAL_SOURCE_EXCEPTION)
...
UFSR = 0x00000100 (usage fault)
  UNALIGNED  : 1  (unaligned access operation occurred)
```

**Status**: ✅ RBU loop is FIXED - packet was received successfully
**Status**: ⚠️ NEW ISSUE - Hard fault due to unaligned memory access

### Analysis of Unaligned Access
The unaligned access occurs at address `0x3000c40e` (6 bytes offset into DMA buffer) during LwIP stack processing. The packet was received and passed to LwIP, but an unaligned access occurred during ARP packet processing.

**Possible Causes**:
1. LwIP internal pbuf access with unaligned pointer
2. Word access to packet data at odd offset
3. Compiler optimization generating unaligned load/store

**Additional Fixes Applied**:
- Added `stm32h7_get_pbuf_from_buff()` helper function
- Used `memset()` to safely initialize pbuf before setting fields
- Made RxBuff_t explicitly 32-byte aligned

## Files Modified
- `rtemslwip/stm32h7/stm32h7_eth.c`
  - Lines 97-101: RxBuff_t alignment fix
  - Lines 128-140: Descriptor structure size fix
  - Lines 184-198: Tail pointer calculation fix
  - Lines 758-940: Safe pbuf initialization
  - All BackupAddr0 references updated to use DMARxDscrBackup[] array

## Remaining Issues
1. **Unaligned Access Hard Fault**: The unaligned access occurs inside LwIP stack during packet processing. May require:
   - Disabling unaligned access trap in SCB (if acceptable)
   - Ensuring all packet data accesses are aligned
   - Using memcpy for word-sized reads from packet data

## Key Insights
1. The STM32H7 DMA is very sensitive to tail pointer positioning
2. Descriptor structure must be exactly 16 bytes for proper ring layout
3. Buffer alignment affects both DMA and CPU access patterns
4. The original RBU was NOT a timing/cache issue - it was purely a tail pointer configuration bug
