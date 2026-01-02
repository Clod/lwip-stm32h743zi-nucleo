# STM32H7 Ethernet Packet Loss - Debugging Summary

**Date**: 2026-01-01  
**Board**: STM32H743ZI Nucleo  
**RTEMS Version**: 6.1  
**Issue**: Network connectivity fails after 6 ping responses

---

## Executive Summary

The STM32H7 Nucleo board running RTEMS 6.1 with LwIP experiences a consistent network failure pattern: exactly 6 ICMP ping responses succeed, then all further communication fails. Investigation revealed this is a **TX descriptor exhaustion issue** caused by a mismatch between the application code and the pre-compiled RTEMS BSP HAL library.

**Root Cause**: The HAL library in the RTEMS BSP was compiled with `ETH_RX_DESC_CNT=4`, but attempts to work around HAL limitations by manually starting the Ethernet DMA break TX interrupt handling, causing TX to get stuck at descriptor index 3.

---

## Problem Description

### Symptoms

1. **Consistent failure pattern**: Network responds to exactly 6 ping requests, then stops
2. **RX continues working**: Board receives packets indefinitely (RxCplt counter keeps incrementing)
3. **TX stops working**: After 6 transmissions, TX descriptor index (`TxIdx`) gets stuck at 3
4. **No errors reported**: No DMA errors, no HAL errors, system appears healthy

### Initial Observations

```bash
$ ping 192.168.1.10
64 bytes from 192.168.1.10: icmp_seq=1 ttl=255 time=0.315 ms
64 bytes from 192.168.1.10: icmp_seq=2 ttl=255 time=0.251 ms
64 bytes from 192.168.1.10: icmp_seq=3 ttl=255 time=0.230 ms
64 bytes from 192.168.1.10: icmp_seq=4 ttl=255 time=0.266 ms
64 bytes from 192.168.1.10: icmp_seq=5 ttl=255 time=0.289 ms
64 bytes from 192.168.1.10: icmp_seq=6 ttl=255 time=0.262 ms
From 192.168.1.20 icmp_seq=7 Destination Host Unreachable
```

### Diagnostic Output

```
HB: IRQs=6 Rx=3 RxIdx=3/3/4 TxIdx=3
HB: IRQs=10 Rx=5 RxIdx=1/1/4 TxIdx=1
HB: IRQs=14 Rx=7 RxIdx=3/3/4 TxIdx=3
HB: IRQs=16 Rx=9 RxIdx=1/1/4 TxIdx=3  ← TX stuck at 3
HB: IRQs=18 Rx=11 RxIdx=3/3/4 TxIdx=3  ← TX still stuck
HB: IRQs=19 Rx=12 RxIdx=0/0/4 TxIdx=3  ← RX working, TX stuck
```

**Key observation**: `TxIdx` advances to 3 and then never changes, while `Rx` (RX completion count) continues incrementing.

---

## Investigation History

### Attempt 1: Increase RX Descriptors (4 → 16)

**Hypothesis**: Descriptor ring exhaustion causing packet drops.

**Action**: Modified `ETH_RX_DESC_CNT` from 4 to 16 in:
- `rtemslwip/stm32h7/include/stm32h7xx_hal_conf.h`
- `incoming/stm32h7xx_hal_conf.h`

**Result**: ❌ **Failed**
- Build succeeded but `ETH_RX_DESC_CNT` at runtime still showed 4
- Discovered the BSP's installed header was being used instead

### Attempt 2: Modify BSP Header Directly

**Discovery**: The actual header file used during compilation is:
```
/opt/rtems/6.1/arm-rtems6/nucleo-h743zi/lib/include/stm32h7xx_hal_conf.h
```

**Action**: Modified BSP header to set `ETH_RX_DESC_CNT=16`

**Result**: ❌ **Failed**
- Runtime now showed 16 descriptors
- But `HAL_ETH_Start_IT()` failed with error
- `sizeof(ETH_HandleTypeDef)` changed from 176 to 224 bytes
- **Root cause identified**: HAL library was compiled with 4 descriptors, causing structure size mismatch

### Attempt 3: Bypass HAL_ETH_Start_IT

**Hypothesis**: Structure mismatch prevents HAL_ETH_Start_IT from working; manually start DMA instead.

**Action**: Implemented manual Ethernet DMA start:
```c
/* Enable MAC transmission and reception */
SET_BIT(heth.Instance->MACCR, ETH_MACCR_TE | ETH_MACCR_RE);

/* Enable DMA transmission and reception */
SET_BIT(heth.Instance->DMACTCR, ETH_DMACTCR_ST);  
SET_BIT(heth.Instance->DMACRCR, ETH_DMACRCR_SR);

/* Set state */
heth.gState = HAL_ETH_STATE_STARTED;

/* Set tail pointers */
heth.Instance->DMACRDTPR = (uint32_t)(DMARxDscrTab + ETH_RX_DESC_CNT);
heth.Instance->DMACTDTPR = (uint32_t)(DMATxDscrTab + ETH_TX_DESC_CNT);
```

**Result**: ⚠️ **Partial Success**
- RX works perfectly (with 4 or 16 descriptors)
- TX works for exactly 6 transmissions, then gets stuck at descriptor index 3
- TX completion callback (`HAL_ETH_TxCpltCallback`) stops being called

### Attempt 4: Increase Buffer Pool Size

**Hypothesis**: Running out of buffers, not descriptors.

**Action**: Increased `ETH_RX_BUFFER_CNT` from 32 to 64

**Result**: ❌ **No improvement** (still fails after 6 pings)

### Attempt 5: Disable Trace Logging

**Hypothesis**: Verbose trace output causing timing issues.

**Action**: Disabled all `TRACE_PRINTF` statements

**Result**: ❌ **No improvement** (still fails after 6 pings)

---

## Root Cause Analysis

### The Fundamental Problem

The RTEMS BSP's HAL library (`libstm32h7_hal.a` or similar) was **compiled** with `ETH_RX_DESC_CNT=4`. This means:

1. The HAL library's internal functions expect a 4-descriptor configuration
2. The `ETH_HandleTypeDef` structure is sized for 4 descriptors
3. Changing the header after compilation creates a binary incompatibility

### Why TX Fails at Descriptor 3

When we bypass `HAL_ETH_Start_IT()`:
- The HAL's internal TX state machine isn't fully initialized
- `HAL_ETH_Transmit_IT()` still works for the first ~6 transmissions
- After wrapping around the 4-descriptor ring (indices 0,1,2,3,0,1,2,3...), something breaks
- TX completion interrupts stop firing
- The `TxPktSemaphore` is never signaled for descriptor 3 and beyond
- TX thread blocks forever waiting for the semaphore

### Why RX Continues Working

RX packet processing in our modified code doesn't rely as heavily on HAL internal state:
- We manually refill descriptors in `low_level_input()`
- We manually kick the RX DMA with tail pointer updates
- RX completion interrupts continue to fire properly

---

## Current Code State

### Modified Files

1. **`rtemslwip/stm32h7/stm32h7_eth.c`**
   - Bypasses `HAL_ETH_Start_IT()` with manual DMA start (lines 1528-1555)
   - Implements manual RX descriptor refilling (lines 995-1030)
   - Increased `ETH_RX_BUFFER_CNT` from 32 to 64 (line 112)
   - Moved TX bounce buffer to 0x30020000 to avoid overlap (line 116)
   - Added diagnostic output for descriptor state

2. **`rtemslwip/test/stm32h7_test/stm32h7_test.c`**
   - Added heartbeat diagnostic output showing TX/RX descriptor indices (lines 57-67)

3. **`README.md`**
   - Added build instructions for STM32H743ZI Nucleo
   - Documented (now-known-to-be-problematic) BSP header modification approach

4. **BSP Header (External)**
   - `/opt/rtems/6.1/arm-rtems6/nucleo-h743zi/lib/include/stm32h7xx_hal_conf.h`
   - Currently set to `ETH_RX_DESC_CNT=4` (reverted from 16)

### Memory Layout

```
D2 SRAM Memory Map:
┌─────────────────────────────────────────┐
│ 0x30000000: RX Descriptors (256 bytes)  │  16 desc × 16 bytes (or 4 × 16 = 64)
│ 0x30000400: TX Descriptors (64 bytes)   │  4 desc × 16 bytes
│ 0x30000600: RX Buffer Pool              │  64 buffers × 1552 bytes = 99,328 bytes
│ 0x30020000: TX Bounce Buffer (1536 B)   │  Single buffer for TX
└─────────────────────────────────────────┘
```

---

## Solution Pathways

### Option 1: Rebuild RTEMS BSP (RECOMMENDED)

This is the **proper** solution that eliminates HAL compatibility issues.

#### Prerequisites

1. **RTEMS Source Code**
   ```bash
   git clone https://github.com/RTEMS/rtems.git
   cd rtems
   git checkout 6.1
   ```

2. **RTEMS Source Builder (RSB)**
   ```bash
   git clone https://github.com/RTEMS/rtems-source-builder.git
   cd rtems-source-builder
   git checkout 6.1
   ```

3. **Build toolchain** (if not already done)
   ```bash
   cd rtems-source-builder/rtems
   ../source-builder/sb-set-builder \
     --prefix=/opt/rtems/6.1 \
     6/rtems-arm
   ```

#### Step-by-Step BSP Rebuild

**Step 1**: Locate the BSP's HAL configuration

The STM32H7 BSP configuration for Nucleo-H743ZI is typically in:
```
rtems/bsps/arm/stm32h7/include/stm32h7xx_hal_conf.h
```

**Step 2**: Modify the configuration **before** building RTEMS

Edit `rtems/bsps/arm/stm32h7/include/stm32h7xx_hal_conf.h`:

```c
/* ########################### Ethernet Configuration ######################### */
#define ETH_TX_DESC_CNT         4U   /* number of Ethernet Tx DMA descriptors */
#define ETH_RX_DESC_CNT         16U  /* CHANGE FROM 4U to 16U */
```

**Step 3**: Configure RTEMS for your BSP

```bash
cd rtems
./waf configure \
  --prefix=/opt/rtems/6.1-custom \
  --rtems-bsps=arm/nucleo-h743zi \
  --enable-networking
```

**Step 4**: Build and install RTEMS

```bash
./waf build
./waf install
```

This will create a new RTEMS installation at `/opt/rtems/6.1-custom` with the modified BSP.

**Step 5**: Update your project to use the custom RTEMS

In your LwIP project:

```bash
cd /home/utndev/src/lw-ip-stm-32-h-743-zi-nucleo
./waf configure \
  --rtems-bsps=arm/nucleo-h743zi \
  --rtems=/opt/rtems/6.1-custom \  # Point to custom build
  --rtems-version=6
./waf build
```

**Step 6**: Revert code changes

After rebuilding the BSP, you can revert the manual DMA start workaround:

1. Remove the manual start code (lines 1528-1555 in `stm32h7_eth.c`)
2. Use `HAL_ETH_Start_IT(&heth)` normally
3. The HAL will now work correctly with 16 descriptors

#### Advantages

✅ Clean solution - no workarounds  
✅ HAL functions work as designed  
✅ TX and RX both fully functional  
✅ Can use HAL debugging and error handling  
✅ Future BSP updates can incorporate your changes  

#### Disadvantages

⏰ Time-consuming (full RTEMS rebuild takes 30-60 minutes)  
💾 Requires significant disk space (~2-3 GB for source + build artifacts)  
🔄 Need to maintain custom BSP across RTEMS updates  

---

### Option 2: Implement Manual TX Descriptor Management (WORKAROUND)

Bypass the HAL entirely for TX, managing descriptors manually like we do for RX.

#### Implementation Overview

Replace `HAL_ETH_Transmit_IT()` with direct descriptor manipulation:

```c
static err_t low_level_output_manual(struct netif *netif, struct pbuf *p) {
  // 1. Get next available TX descriptor
  uint32_t tx_idx = tx_descriptor_index;
  ETH_DMADescTypeDef *txdesc = &DMATxDscrTab[tx_idx];
  
  // 2. Wait for descriptor to be free (OWN=0)
  while (txdesc->DESC3 & 0x80000000) {
    rtems_task_wake_after(1); // Descriptor still owned by DMA
  }
  
  // 3. Copy packet to TX buffer
  memcpy((void*)ETH_TX_BUFFER_ADDR, pbuf_data, pbuf_len);
  SCB_CleanDCache_by_Addr(ETH_TX_BUFFER_ADDR, pbuf_len);
  
  // 4. Setup descriptor
  txdesc->DESC0 = ETH_TX_BUFFER_ADDR;
  txdesc->DESC2 = pbuf_len;
  txdesc->DESC3 = 0xB0000000; // OWN | FD | LD | IC
  SCB_CleanDCache_by_Addr(txdesc, sizeof(*txdesc));
  __DSB();
  
  // 5. Kick TX DMA
  heth.Instance->DMACTDTPR = (uint32_t)(&DMATxDscrTab[(tx_idx+1) % ETH_TX_DESC_CNT]);
  
  // 6. Advance index
  tx_descriptor_index = (tx_idx + 1) % ETH_TX_DESC_CNT;
  
  return ERR_OK;
}
```

#### Advantages

⚡ Quick to implement  
🎯 Full control over TX behavior  
🔍 Easier to debug TX issues  

#### Disadvantages

⚠️ Bypasses HAL error handling  
🐛 More complex, more chances for bugs  
📝 Must implement TX timeout/cleanup logic  
🔄 Semaphore handling becomes manual  

---

### Option 3: Use Polling Mode (QUICK WORKAROUND)

Replace interrupt-driven TX with polling.

```c
static err_t low_level_output_polling(struct netif *netif, struct pbuf *p) {
  HAL_StatusTypeDef status;
  
  // Use HAL_ETH_Transmit instead of HAL_ETH_Transmit_IT
  status = HAL_ETH_Transmit(&heth, &TxConfig, 100); // 100ms timeout
  
  if (status != HAL_OK) {
    return ERR_TIMEOUT;
  }
  
  return ERR_OK;
}
```

#### Advantages

🚀 Simplest workaround (2-line change)  
✅ Avoids interrupt issues  

#### Disadvantages

⏱️ Blocks during transmission  
📉 Lower throughput  
💤 Wastes CPU cycles polling  

---

## Recommended Action Plan

### Immediate Steps (This Session)

1. ✅ **Document current state** (this document)
2. ⏭️ **Try Option 2**: Implement manual TX descriptor management
3. 📊 **Test**: Verify ping works beyond 6 responses
4. 💾 **Commit changes**: Save working state

### Long-Term Steps (Future Sessions)

1. 🔨 **Rebuild RTEMS BSP** with `ETH_RX_DESC_CNT=16` (Option 1)
2. ♻️ **Revert workarounds**: Use HAL functions normally
3. 🧪 **Full testing**: iperf, sustained traffic, stress tests
4. 📝 **Update README**: Document the custom BSP requirement

---

## Key Files Reference

### Configuration Files

| File | Purpose | Current State |
|------|---------|---------------|
| `/opt/rtems/6.1/arm-rtems6/nucleo-h743zi/lib/include/stm32h7xx_hal_conf.h` | BSP's HAL config (used during compilation) | `ETH_RX_DESC_CNT=4` |
| `rtemslwip/stm32h7/include/stm32h7xx_hal_conf.h` | Project's HAL config (NOT used) | `ETH_RX_DESC_CNT=16` |
| `incoming/stm32h7xx_hal_conf.h` | Incoming reference (NOT used) | `ETH_RX_DESC_CNT=16` |

### Source Files

| File | Key Modifications |
|------|-------------------|
| `stm32h7_eth.c` | Manual DMA start (line 1528), RX descriptor refill (line 995), 64 buffers (line 112) |
| `stm32h7_test.c` | Diagnostic heartbeat output (line 60) |
| `trace_config.h` | Trace mode control (line 5, currently disabled) |

### Build Commands

```bash
# Clean build
./waf configure --rtems-bsps=arm/nucleo-h743zi --rtems=/opt/rtems/6.1 --rtems-version=6
./waf build

# Copy firmware
cp build/arm-rtems6-nucleo-h743zi/stm32h7_test.exe ~/work/stm32h7_test.elf
```

---

## Diagnostic Commands

### Check BSP Header

```bash
grep ETH_RX_DESC_CNT /opt/rtems/6.1/arm-rtems6/nucleo-h743zi/lib/include/stm32h7xx_hal_conf.h
```

### Monitor Network

```bash
# Test ping (should work for 6 responses, then fail)
ping -c 20 192.168.1.10

# Monitor serial output for diagnostics
# Look for: HB: IRQs=X Rx=Y RxIdx=A/B/C TxIdx=D
# TxIdx stuck at 3 indicates the known TX bug
```

### Check Descriptor State

Serial output format:
```
HB: IRQs=<total_interrupts> Rx=<rx_complete_count> RxIdx=<rx_idx>/<rx_build_idx>/<rx_build_cnt> TxIdx=<tx_idx>
```

- If `TxIdx` stops advancing: TX is stuck
- If `Rx` keeps incrementing: RX is working
- If `IRQs` stops incrementing: No interrupts (bigger problem)

---

## Success Criteria

A successful fix will show:

✅ **Sustained ping responses**: `ping -c 100 192.168.1.10` with 0% packet loss  
✅ **TxIdx cycling**: Heartbeat shows `TxIdx` cycling through 0→1→2→3→0→...  
✅ **Good throughput**: `iperf -c 192.168.1.10` shows >10 Mbits/sec  
✅ **No stuck descriptors**: RxIdx and TxIdx both keep advancing  

---

## Additional Notes

### Why 6 Transmissions?

The pattern of exactly 6 successful transmissions before failure suggests:
- First ARP reply: Uses TX descriptor 0
- Ping 1 reply: TX descriptor 1
- Ping 2 reply: TX descriptor 2  
- Ping 3 reply: TX descriptor 3
- Ping 4 reply: TX descriptor 0 (wrap)
- Ping 5 reply: TX descriptor 1
- Ping 6 reply: TX descriptor 2
- **Ping 7**: Tries to use descriptor 3 again → **FAILS**

Something about reusing descriptor 3 after wrap-around breaks TX interrupt handling.

### BSP Location

RTEMS BSP files are installed to:
```
/opt/rtems/<version>/<arch>-rtems<version>/<bsp>/
```

For our board:
```
/opt/rtems/6.1/arm-rtems6/nucleo-h743zi/
├── lib/
│   ├── include/          ← HAL headers
│   └── *.a               ← Compiled libraries
└── ...
```

### Alternative: Use Different Board

If rebuilding the BSP is not feasible, consider using a board with better RTEMS support or pre-configured for higher descriptor counts. However, this requires different hardware.

---

## Contact & Resources

- **RTEMS Documentation**: https://docs.rtems.org/
- **STM32H7 HAL**: https://github.com/STMicroelectronics/STM32CubeH7
- **LwIP**: https://savannah.nongnu.org/projects/lwip/

---

**Document Version**: 1.0  
**Last Updated**: 2026-01-01 19:35 UTC
