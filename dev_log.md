# Development Log

## MOD-001 [2025-12-29 18:18:00]
**Category**: Investigation
**Modified Files**: N/A
**Description**: Initial investigation of NULL pointer dereference crash.
**Rationale**: Application crashes immediately after first packet manual read or during processing.
**Implementation**: Analyzing register dump: PC=0x0800b572, LR=0x0800b323, MMFAR=0x00000000.
**Testing & Results**:
- **Test Method**: addr2line on PC/LR
- **Expected Outcome**: Identify file and line of crash.
- **Actual Outcome**: Crash identified in `ethernet_input` at line 101 due to NULL `p->payload`.
- **Status**: ✓ Success
**Context**:
- **Related Entries**: N/A
- **Side Effects**: N/A
- **Next Steps**: Implement `pbuf_alloced_custom` in manual read fallback.

## MOD-002 [2025-12-29 18:22:00]
**Category**: Bug Fix
**Modified Files**: `rtemslwip/stm32h7/stm32h7_eth.c:814-826`
**Description**: Fix NULL pointer dereference in `low_level_input` manual read fallback.
**Rationale**: The manual read fallback didn't initialize `pbuf->payload`, causing a crash in `lwip/src/netif/ethernet.c:101`.
**Implementation**: Replaced manual field initialization with `pbuf_alloced_custom(PBUF_RAW, pkt_len, PBUF_REF, p_custom, buff, ETH_RX_BUFFER_SIZE)`.
**Testing & Results**:
- **Test Method**: Compilation and user verification.
- **Expected Outcome**: Stable packet reception without crashing.
- **Actual Outcome**: NULL pointer crash fixed, but triggered `UNALIGNED` usage fault at `acd.c:388` (PC=0x0800af48).
- **Status**: ⚠ Partial
**Context**:
- **Related Entries**: MOD-001
- **Side Effects**: N/A
- **Next Steps**: Implement alignment-safe copy macros in `cc.h`.

## MOD-003 [2025-12-29 18:35:00]
**Category**: Bug Fix
**Modified Files**: `rtemslwip/include/arch/cc.h:125-135`
**Description**: Implement alignment-safe IP address copy macros.
**Rationale**: The Cortex-M7 with `CCR.UNALIGN_TRP=1` traps 32-bit loads from 2-byte aligned addresses, which occur in ARP/IP headers because Ethernet headers are 14 bytes and DMA buffers are 4-byte aligned.
**Implementation**: Redefined `IPADDR_WORDALIGNED_COPY_TO_IP4_ADDR_T` and `IPADDR_WORDALIGNED_COPY_FROM_IP4_ADDR_T` to use byte-wise copying.
**Testing & Results**:
- **Test Method**: Compilation and user verification.
- **Expected Outcome**: ARP/IP processing continues without alignment fault.
- **Actual Outcome**: Build successful. Safe copy macros implemented.
- **Status**: ✓ Success
**Context**:
- **Related Entries**: MOD-002
- **Side Effects**: N/A
- **Next Steps**: Implement alignment-safe copy macros in `cc.h`.

## MOD-004 [2025-12-29 18:45:00]
**Category**: Bug Fix / Configuration
**Modified Files**: `rtemslwip/stm32h7/stm32h7_eth.c:1515`
**Description**: Disable Cortex-M7 unaligned access trap.
**Rationale**: The initial `MOD-003` only fixed one macro. A second crash occurred in `ip4.c:619` because standard IP header fields are unaligned when `ETH_PAD_SIZE=0` and DMA buffers are 4-byte aligned. STM32H7 hardware supports unaligned access, so disabling the trap is the most efficient fix.
**Implementation**: Added `SCB->CCR &= ~SCB_CCR_UNALIGN_TRP_Msk;` in `MPU_Config`.
**Testing & Results**:
- **Test Method**: Compilation and user verification.
- **Expected Outcome**: All header accesses (16-bit and 32-bit) succeed without fault.
- **Actual Outcome**: Continued `UNALIGNED` usage fault at `ip4.c:549` (PC=0x0801d7f4). ARP (type 806) passed, but IPv4 (type 800) failed.
- **Status**: ⚠ Partial

## MOD-005 [2025-12-29 18:46:00]
**Category**: Bug Fix / Safety
**Modified Files**: `rtemslwip/include/arch/cc.h:138-140`
**Description**: Redefine `ip4_addr_copy` to use `SMEMCPY`.
**Rationale**: For extra safety, ensure all IP address copies use byte-wise copying, as these are the most common 32-bit unaligned accesses in LWIP.
**Implementation**: Added `#define ip4_addr_copy(dest, src) SMEMCPY(&(dest), &(src), sizeof(ip4_addr_t))` to `cc.h`.
**Testing & Results**:
- **Test Method**: Compilation.
- **Expected Outcome**: Build successful.
- **Actual Outcome**: Build successful. Redefinition didn't prevent crash (likely due to struct-by-value pass in `ip_addr_copy_from_ip4`).
- **Status**: ⚠ Partial
**Context**:
- **Related Entries**: MOD-003, MOD-004
- **Side Effects**: N/A
- **Next Steps**: Implement `ETH_PAD_SIZE=2`.

## MOD-006 [2025-12-29 19:15:00] (completed 2025-12-29 19:22:00)
**Category**: Performance / Stability
**Modified Files**: `rtemslwip/include/lwipopts.h`, `rtemslwip/stm32h7/stm32h7_eth.c`
**Description**: Implement `ETH_PAD_SIZE=2` for 4-byte aligned IP headers.
**Rationale**: Disabling `CCR.UNALIGN_TRP` failed to prevent crashes in IPv4 processing. The most reliable solution is to pad the Ethernet header with 2 bytes so that the IP header (at offset 14) starts at offset 16 (aligned).
**Implementation**:
- Defined `ETH_PAD_SIZE 2` in `lwipopts.h` (lines 251-253).
- Offset RX buffer DMA address by 2 bytes in `HAL_ETH_RxAllocateCallback` (line 1428): `*buff = (uint8_t *)p + offsetof(RxBuff_t, buff) + 2`.
- Descriptor initialization in `ethernet_link_thread` (line 1315) automatically uses the offset pointer from `HAL_ETH_RxAllocateCallback`.
- Adjusted `pbuf` initialization in `low_level_input` manual read fallback (lines 816-824) to use the offset buffer.
- Updated `HAL_ETH_RxLinkCallback` (line 1478) cache invalidation to account for the 2-byte offset: `SCB_InvalidateDCache_by_Addr((uint32_t *)(buff - 2), Length)`.
**Testing & Results**:
- **Test Method**: Build and user verification.
- **Expected Outcome**: Stable IP communication without alignment faults.
- **Actual Outcome**: ⏳ Pending (requires hardware test)
- **Status**: ✓ Implementation Complete

## MOD-007 [2025-12-29 19:30:00] (completed 2025-12-29 19:31:00)
**Category**: Bug Fix
**Modified Files**: `rtemslwip/stm32h7/stm32h7_eth.c`
**Description**: Fix pbuf payload calculation for ETH_PAD_SIZE=2 offset buffers.
**Rationale**: When passing offset buffer (buff+2) to `pbuf_alloced_custom`, the internal payload calculation was incorrect, causing payload to point 2 bytes before the actual data.
**Implementation**:
- In `low_level_input` manual read fallback (lines 816-830): Calculate actual buffer start (buff-2), pass to `pbuf_alloced_custom`, then manually adjust payload: `p->payload = (uint8_t *)p->payload + 2`.
- In `HAL_ETH_RxLinkCallback` (lines 1443-1481): Calculate actual buffer start, create pbuf from actual buffer, then adjust payload by +2.
**Testing & Results**:
- **Test Method**: Build and user verification.
- **Expected Outcome**: Correct pbuf payload pointing to Ethernet header.
- **Actual Outcome**: ⏳ Pending (requires hardware test)
- **Status**: ✓ Implementation Complete

---

# SESSION SUMMARY [2025-12-29 19:32:00]

## Session Overview
**Entries**: MOD-006 through MOD-007
**Total Modifications**: 2 entries
**Files Modified**: `rtemslwip/include/lwipopts.h`, `rtemslwip/stm32h7/stm32h7_eth.c`

## Overall Status
**Phase**: ETH_PAD_SIZE=2 Implementation (In Progress)
**Goal**: Achieve stable IPv4 packet processing without alignment faults on Cortex-M7

## Critical Issues Still Unresolved
1. **UNALIGNED Usage Fault** - Despite ETH_PAD_SIZE=2 implementation, unaligned access may still occur if any code path accesses IP header fields at unaligned addresses
2. **HAL_ETH_ReadData Reliability** - The HAL function may return NULL or fail, requiring manual read fallback
3. **Descriptor Management** - Complex interaction between HAL descriptor recycling and manual read path

## Next Session Priorities
1. **Test MOD-006/MOD-007 fixes** - Build and verify pbuf payload is correctly aligned
2. **Monitor for remaining alignment faults** - If UNALIGNED faults persist, investigate other code paths
3. **Consider alternative: ETH_RX_BUFFER_ALIGNMENT** - If ETH_PAD_SIZE=2 fails, consider using `LWIP_DEC_MEMORY_ALIGNMENT` or modifying DMA buffer alignment
4. **Performance optimization** - Once stable, benchmark network throughput

## MOD-008 [2025-12-29 19:50:00]
**Category**: Bug Fix / Optimization
**Modified Files**: `rtemslwip/stm32h7/stm32h7_eth.c`
**Description**: Replace `low_level_input` with manual descriptor processing loops and increase `ETH_RX_DESC_CNT` to 12.
**Rationale**: `HAL_ETH_ReadData` fails with `ETH_PAD_SIZE=2` due to alignment offset mismatches. The manual read logic has been verified to work. Increasing descriptors prevents RBU during bursts.
**Implementation**:
- Changed `ETH_RX_DESC_CNT` from 4 to 12.
- Replaced `low_level_input` to directly scan RX descriptors, checking `OWN` bit.
- Integrated `eth_pad` offset logic (+2 bytes) directly into the read loop.
- Implemented immediate descriptor recycling/refill within the read loop to minimize latency.
**Test Results**:
- **Test Method**: Hardware verification.
- **Expected Outcome**: Stable operation, no RBU, packet reception.
- **Actual Outcome**: System stable. `HAL_ETH_ReadData` and RBU errors eliminated. IRQs firing correctly (Count=8). However, `RX: ...` logs missing, suggesting `low_level_input` might not be successfully retrieving packets despite IRQs.
- **Status**: ✓ Success (Stability achieved)
**Context**:
- **Related Entries**: MOD-006, MOD-007
- **Side Effects**: RX Data path might be silently dropping packets or logging is suppressed.
- **Next Steps**: Debug `low_level_input` content retrieval.

## MOD-009 [2025-12-29 20:00:00]
**Category**: Bug Fix
**Modified Files**: `rtemslwip/stm32h7/stm32h7_eth.c`
**Description**: Debug and fix silent packet dropping in `low_level_input`.
**Rationale**: IRQs are firing (DMA completing), but the application stack is not reporting received bytes. This implies `low_level_input` is returning NULL, possibly due to `RxDescIdx` desync or cache invalidation issues preventing the CPU from seeing the `OWN` bit flip.
**Implementation Plan**:
- Add diagnostic prints in `low_level_input` to show `RxDescIdx` and Descriptor flags when called.
- Verify D-Cache invalidation scope.
- Check if `RxDescIdx` needs to loop/scan forward if multiple packets arrive.
---

# SESSION SUMMARY [2025-12-29 20:05:00]

## Session Overview
**Entries**: MOD-008 through MOD-009
**Total Modifications**: 1 implementation + 1 plan
**Files Modified**: `rtemslwip/stm32h7/stm32h7_eth.c`

## Overall Status
**Phase**: Stabilization & RBU Fix (Success) -> Data Path Debug (Checking)
**Goal**: The system has achieved electrical/driver stability (no crashes, no RBU loops). The focus is now on ensuring the data actually reaches the application.

## Critical Issues Still Unresolved
1.  **Silent Packet Drop**: IRQs fire (Count=8), meaning the hardware is receiving and DMA is writing, but the application stack (`RX: ...` logs) sees nothing. This indicates a logic gap in `low_level_input` where it fails to pick up the packet despite the descriptor being owned by CPU or the cache being stale.

## Next Session Priorities
1.  **Execute MOD-009**: Debug `low_level_input`.
    *   Add printfs to see `RxDescIdx`, `DESC3` values, and ownership bits during the IRQ/Thread cycle.
    *   Verify if `SCB_InvalidateDCache` is working effectively on the new descriptor 12-ring.
2.  **Verify Data Integrity**: Once `RX` logs appear, verify the payload content is correct (ping/IP headers).

## MOD-010 [2025-12-30 21:55:00]
**Category**: Bug Fix
**Modified Files**: `rtemslwip/stm32h7/stm32h7_eth.c`
**Description**: Fix TX packet corruption by skipping `ETH_PAD_SIZE` padding bytes.
**Rationale**: With `ETH_PAD_SIZE=2`, LwIP adds 2 bytes of padding to the pbuf payload. The driver was transmitting these bytes, resulting in a malformed Ethernet frame (shifted by 2 bytes).
**Implementation**: Added `pbuf_header(p, -ETH_PAD_SIZE)` before the copy loop in `low_level_output` and restored it with `pbuf_header(p, ETH_PAD_SIZE)` afterwards.
**Testing & Results**:
- **Test Method**: Wireshark/Packet Dump.
- **Expected Outcome**: Correct Destination MAC address in transmitted packets.
- **Actual Outcome**: ⏳ Pending
- **Status**: ✓ Implementation Complete
**Context**:
- **Related Entries**: MOD-006
- **Side Effects**: None expected.
- **Next Steps**: Verify TX packet format.

----------------

Here is the breakdown of what is happening based on the logs and the behavior of the STM32H7 Ethernet DMA:

Packet Reception (Working): The DMA successfully receives packets from the PHY, writes the data into the RAM buffers (e.g., 0x3000b7b6), and updates the descriptors.
Handover to CPU (Working): The DMA clears the OWN bit in the descriptor (setting it to 0), which signals to the CPU that "this slot is full, you can read it now." The logs show this happening for Descriptors 0, 1, and 2.
The "Final Slot" Issue (The Bug): The DMA stops exactly before processing the final descriptor (Descriptor 3).
Why it stops at the final slot
The root cause is the configuration of the Tail Pointer (DMACRDTPR).

How it works: The DMA processes descriptors starting from its current position up to—but not including—the address held in the Tail Pointer. It treats the Tail Pointer as a "Stop Sign."
What happened: The logs show DMACRDTPR was set to 0x30000030. This is the address of Descriptor 3 (the 4th and final slot).
The Result:
DMA processes Desc 0.
DMA processes Desc 1.
DMA processes Desc 2.
DMA prepares to process Desc 3 (0x30000030).
DMA checks the Tail Pointer (0x30000030).
Since Current Address == Tail Pointer, the DMA suspends reception immediately, triggering the Receive Buffer Unavailable (RBU) error.
By setting the Tail Pointer to the address of the last descriptor, you inadvertently told the DMA: "Use the buffer, but stop right before the last slot."

The fix (which appears in the code you provided in the context) moves the Tail Pointer to Start + 4, effectively placing the "Stop Sign" outside the ring buffer. This allows the DMA to process Descriptor 3 and wrap around to Descriptor 0 continuously.


-------------------------------------

*** STM32H7 LWIP TEST ***
MPU_Config: CCR before = 0x00070200
MPU_Config: CCR after  = 0x00070200
DEBUG: sizeof(ETH_HandleTypeDef) = 176
DEBUG: ETH_RX_DESC_CNT (Local Define) = 4
DEBUG: offsetof(RxDescList) = 72
DEBUG: offsetof(RxDescIdx) = 92
Ethernet MAC: 00:80:e1:00:00:00
Initializing RX pool at 0x30000600...
ETH: DMA Descriptors committed to hardware: RXaddr=0x30000000, RXlen=4, TXaddr=0x30000400, TXlen=4
Installing ETH interrupt handler for vector 61...
ETH interrupt handler installed successfully
ETH: IRQ 61 enabled in NVIC
Setting MAC address...
Creating RxPktSemaphore...
Creating TxPktSemaphore...
Registering PHY IO...
LAN8742 object at: 0x0x24001ee0
IOCtx at: 0x0x240000f0
IOCtx.Init = 0x0x800de69
IOCtx.DeInit = 0x0x800de7d
IOCtx.WriteReg = 0x0x800dec1
IOCtx.ReadReg = 0x0x800de8d
IOCtx.GetTick = 0x0x800def5
Testing HAL_GetTick: 90
After 100ms delay, HAL_GetTick: 190
Creating Ethernet threads...
Waiting for Ethernet link to come up...
etharp_timer
ethernetif_input thread started, netif=0x0x2400061c
Ethernet link thread started
PHY: Resetting...
PHY: Enabling auto-negotiation...
etharp_timer
PHY: Link Up detected (BSR=0x782d)
Negotiated: 100Mbps Full Duplex
ETH: Manually initializing RX descriptors...

========== RX_ALLOCATE_CALLBACK ==========
RX_ALLOC: Allocated pbuf_custom at 0x0x3000c3e0
RX_ALLOC: buff=0x0x3000c3f6 (with +2 offset), actual buff=0x0x3000c3f4
RX_ALLOC: pbuf initialized successfully
========== RX_ALLOCATE_CALLBACK END ==========

RX Init Desc 0: addr=0x3000c3f6, bkup=0x3000c3f6, len=1536, DESC3=0xc1000000

========== RX_ALLOCATE_CALLBACK ==========
RX_ALLOC: Allocated pbuf_custom at 0x0x3000bdc0
RX_ALLOC: buff=0x0x3000bdd6 (with +2 offset), actual buff=0x0x3000bdd4
RX_ALLOC: pbuf initialized successfully
========== RX_ALLOCATE_CALLBACK END ==========

RX Init Desc 1: addr=0x3000bdd6, bkup=0x3000bdd6, len=1536, DESC3=0xc1000000

========== RX_ALLOCATE_CALLBACK ==========
RX_ALLOC: Allocated pbuf_custom at 0x0x3000b7a0
RX_ALLOC: buff=0x0x3000b7b6 (with +2 offset), actual buff=0x0x3000b7b4
RX_ALLOC: pbuf initialized successfully
========== RX_ALLOCATE_CALLBACK END ==========

RX Init Desc 2: addr=0x3000b7b6, bkup=0x3000b7b6, len=1536, DESC3=0xc1000000

========== RX_ALLOCATE_CALLBACK ==========
RX_ALLOC: Allocated pbuf_custom at 0x0x3000b180
RX_ALLOC: buff=0x0x3000b196 (with +2 offset), actual buff=0x0x3000b194
RX_ALLOC: pbuf initialized successfully
========== RX_ALLOCATE_CALLBACK END ==========

RX Init Desc 3: addr=0x3000b196, bkup=0x3000b196, len=1536, DESC3=0xc1000000
ETH: RX descriptors initialized successfully
HAL_ETH_Start_IT successful
ETH: Enabling DMA interrupts...
ETH: DMACIER = 0x0000d0c1, MACIER = 0x00006000
ETH: DMACSR  = 0x00000000
ETH: SYSCFG_PMCR = 0x03800000
ETH: MACCR = 0x3830e003
RX Desc 0: 0x3000c3f6 0x00000000 0x00000600 0xc1000000
ETH: MACCR = 0x3830e003, MACPFR = 0x80000001, MACTSCR = 0x00000000
Ethernet link is UP
Interface is UP. IP: 192.168.1.10
Starting lwiperf server on port 5001...
tcp_bind: bind to port 5001
Heartbeat: Total IRQs: 0, RX Cplt: 0
etharp_timer
etharp_timer
Heartbeat: Total IRQs: 0, RX Cplt: 0
etharp_timer
etharp_timer
LLI: Single packet detected, len=60 bytes
LLI: Buffer address: backup=0x3000c3f6, actual=0x3000c3f4
LLI: pbuf created successfully, p=0x0x3000c3e0, payload=0x0x3000c3f6
PKT DUMP (60 bytes):
  0000: ff ff ff ff ff ff e8 9a 8f 8e 40 d9 08 06 00 01 
  0010: 08 00 06 04 00 01 e8 9a 8f 8e 40 d9 c0 a8 01 14 
  0020: 00 00 00 00 00 00 c0 a8 01 0a 00 00 00 00 00 00 
  0030: 00 00 00 00 00 00 00 00 00 00 00 00 
LLI: Ethernet Header: ff:ff:ff:ff:ff:ff -> e8:9a:8f:8e:40:d9, type=0x0806

========== RX_ALLOCATE_CALLBACK ==========
RX_ALLOC: Allocated pbuf_custom at 0x0x3000ab60
RX_ALLOC: buff=0x0x3000ab76 (with +2 offset), actual buff=0x0x3000ab74
RX_ALLOC: pbuf initialized successfully
========== RX_ALLOCATE_CALLBACK END ==========

LLI: Refilling descriptor 0 with new buffer 0x3000ab76
LLI: After refill - RxDescIdx=0, RxBuildDescIdx=1, RxBuildDescCnt=4
ETHERNETIF_INPUT: Got packet 60 bytes, passing to netif->input
ETHERNETIF_INPUT: netif->input succeeded
ethernet_input: dest:ff:ff:ff:ff:e8:9a, src:8f:8e:40:d9:08:06, type:1

========== PBUF_FREE_CUSTOM ==========
PBUF_FREE: p=0x0x3000c3e0, ref=0, tot_len=60
PBUF_FREE: custom_pbuf=0x0x3000c3e0, freeing to RX_POOL
========== PBUF_FREE_CUSTOM END ==========

Heartbeat: Total IRQs: 1, RX Cplt: 1
etharp_timer
LLI: Single packet detected, len=60 bytes
LLI: Buffer address: backup=0x3000bdd6, actual=0x3000bdd4
LLI: pbuf created successfully, p=0x0x3000bdc0, payload=0x0x3000bdd6
PKT DUMP (60 bytes):
  0000: ff ff ff ff ff ff e8 9a 8f 8e 40 d9 08 06 00 01 
  0010: 08 00 06 04 00 01 e8 9a 8f 8e 40 d9 c0 a8 01 14 
  0020: 00 00 00 00 00 00 c0 a8 01 0a 00 00 00 00 00 00 
  0030: 00 00 00 00 00 00 00 00 00 00 00 00 
LLI: Ethernet Header: ff:ff:ff:ff:ff:ff -> e8:9a:8f:8e:40:d9, type=0x0806

========== RX_ALLOCATE_CALLBACK ==========
RX_ALLOC: Allocated pbuf_custom at 0x0x3000c3e0
RX_ALLOC: buff=0x0x3000c3f6 (with +2 offset), actual buff=0x0x3000c3f4
RX_ALLOC: pbuf initialized successfully
========== RX_ALLOCATE_CALLBACK END ==========

LLI: Refilling descriptor 1 with new buffer 0x3000c3f6
LLI: After refill - RxDescIdx=1, RxBuildDescIdx=2, RxBuildDescCnt=4
ETHERNETIF_INPUT: Got packet 60 bytes, passing to netif->input
ETHERNETIF_INPUT: netif->input succeeded
ethernet_input: dest:ff:ff:ff:ff:e8:9a, src:8f:8e:40:d9:08:06, type:1

========== PBUF_FREE_CUSTOM ==========
PBUF_FREE: p=0x0x3000bdc0, ref=0, tot_len=60
PBUF_FREE: custom_pbuf=0x0x3000bdc0, freeing to RX_POOL
========== PBUF_FREE_CUSTOM END ==========

etharp_timer
LLI: Single packet detected, len=60 bytes
LLI: Buffer address: backup=0x3000b7b6, actual=0x3000b7b4
LLI: pbuf created successfully, p=0x0x3000b7a0, payload=0x0x3000b7b6
PKT DUMP (60 bytes):
  0000: ff ff ff ff ff ff e8 9a 8f 8e 40 d9 08 06 00 01 
  0010: 08 00 06 04 00 01 e8 9a 8f 8e 40 d9 c0 a8 01 14 
  0020: 00 00 00 00 00 00 c0 a8 01 0a 00 00 00 00 00 00 
  0030: 00 00 00 00 00 00 00 00 00 00 00 00 
LLI: Ethernet Header: ff:ff:ff:ff:ff:ff -> e8:9a:8f:8e:40:d9, type=0x0806

========== RX_ALLOCATE_CALLBACK ==========
RX_ALLOC: Allocated pbuf_custom at 0x0x3000bdc0
RX_ALLOC: buff=0x0x3000bdd6 (with +2 offset), actual buff=0x0x3000bdd4
RX_ALLOC: pbuf initialized successfully
========== RX_ALLOCATE_CALLBACK END ==========

LLI: Refilling descriptor 2 with new buffer 0x3000bdd6
LLI: After refill - RxDescIdx=2, RxBuildDescIdx=3, RxBuildDescCnt=4
ETHERNETIF_INPUT: Got packet 60 bytes, passing to netif->input
ETHERNETIF_INPUT: netif->input succeeded
ethernet_input: dest:ff:ff:ff:ff:e8:9a, src:8f:8e:40:d9:08:06, type:1

========== PBUF_FREE_CUSTOM ==========
PBUF_FREE: p=0x0x3000b7a0, ref=0, tot_len=60
PBUF_FREE: custom_pbuf=0x0x3000b7a0, freeing to RX_POOL
========== PBUF_FREE_CUSTOM END ==========

Heartbeat: Total IRQs: 3, RX Cplt: 3
etharp_timer
LLI: Single packet detected, len=60 bytes
LLI: Buffer address: backup=0x3000b196, actual=0x3000b194
LLI: pbuf created successfully, p=0x0x3000b180, payload=0x0x3000b196
PKT DUMP (60 bytes):
  0000: ff ff ff ff ff ff e8 9a 8f 8e 40 d9 08 06 00 01 
  0010: 08 00 06 04 00 01 e8 9a 8f 8e 40 d9 c0 a8 01 14 
  0020: 00 00 00 00 00 00 c0 a8 01 0a 00 00 00 00 00 00 
  0030: 00 00 00 00 00 00 00 00 00 00 00 00 
LLI: Ethernet Header: ff:ff:ff:ff:ff:ff -> e8:9a:8f:8e:40:d9, type=0x0806

========== RX_ALLOCATE_CALLBACK ==========
RX_ALLOC: Allocated pbuf_custom at 0x0x3000b7a0
RX_ALLOC: buff=0x0x3000b7b6 (with +2 offset), actual buff=0x0x3000b7b4
RX_ALLOC: pbuf initialized successfully
========== RX_ALLOCATE_CALLBACK END ==========

LLI: Refilling descriptor 3 with new buffer 0x3000b7b6
LLI: After refill - RxDescIdx=3, RxBuildDescIdx=0, RxBuildDescCnt=4
ETHERNETIF_INPUT: Got packet 60 bytes, passing to netif->input
ETHERNETIF_INPUT: netif->input succeeded
ethernet_input: dest:ff:ff:ff:ff:e8:9a, src:8f:8e:40:d9:08:06, type:1

========== PBUF_FREE_CUSTOM ==========
PBUF_FREE: p=0x0x3000b180, ref=0, tot_len=60
PBUF_FREE: custom_pbuf=0x0x3000b180, freeing to RX_POOL
========== PBUF_FREE_CUSTOM END ==========

etharp_timer
LLI: Single packet detected, len=60 bytes
LLI: Buffer address: backup=0x3000ab76, actual=0x3000ab74
LLI: pbuf created successfully, p=0x0x3000ab60, payload=0x0x3000ab76
PKT DUMP (60 bytes):
  0000: ff ff ff ff ff ff e8 9a 8f 8e 40 d9 08 06 00 01 
  0010: 08 00 06 04 00 01 e8 9a 8f 8e 40 d9 c0 a8 01 14 
  0020: 00 00 00 00 00 00 c0 a8 01 0a 00 00 00 00 00 00 
  0030: 00 00 00 00 00 00 00 00 00 00 00 00 
LLI: Ethernet Header: ff:ff:ff:ff:ff:ff -> e8:9a:8f:8e:40:d9, type=0x0806

========== RX_ALLOCATE_CALLBACK ==========
RX_ALLOC: Allocated pbuf_custom at 0x0x3000b180
RX_ALLOC: buff=0x0x3000b196 (with +2 offset), actual buff=0x0x3000b194
RX_ALLOC: pbuf initialized successfully
========== RX_ALLOCATE_CALLBACK END ==========

LLI: Refilling descriptor 0 with new buffer 0x3000b196
LLI: After refill - RxDescIdx=0, RxBuildDescIdx=1, RxBuildDescCnt=4
ETHERNETIF_INPUT: Got packet 60 bytes, passing to netif->input
ETHERNETIF_INPUT: netif->input succeeded
ethernet_input: dest:ff:ff:ff:ff:e8:9a, src:8f:8e:40:d9:08:06, type:1

========== PBUF_FREE_CUSTOM ==========
PBUF_FREE: p=0x0x3000ab60, ref=0, tot_len=60
PBUF_FREE: custom_pbuf=0x0x3000ab60, freeing to RX_POOL
========== PBUF_FREE_CUSTOM END ==========

Based on the provided logs, the system is functioning correctly. The previous issues regarding the DMA stopping at the last descriptor and the input loop termination have been resolved.

Here is the breakdown of the successful operation shown in the logs:

Successful Ring Wrap-Around:

The system processed packets in Descriptor 0, 1, 2, and 3 sequentially.
Crucially, after processing Descriptor 3, it successfully wrapped around and processed a new packet in Descriptor 0 again.
This confirms the Tail Pointer fix is working; the DMA no longer treats the last descriptor as a stop sign.
No "Receive Buffer Unavailable" (RBU) Errors:

Unlike previous runs, there are no RBU errors. The software is refilling descriptors fast enough to keep the DMA happy.
Correct Data Flow:

Reception: LLI: Single packet detected finds the packet.
Allocation: RX_ALLOC successfully gets a new buffer for the next DMA transaction.
Refill: LLI: Refilling descriptor X updates the hardware descriptor with the new buffer.
Input: ETHERNETIF_INPUT passes the received packet to the LWIP stack.
Cleanup: PBUF_FREE releases the old buffer back to the pool after LWIP is done with it.
Valid Data:

The PKT DUMP shows valid ARP packets (EtherType 0x0806) being received, indicating the memory offsets and cache invalidation logic are correct.
Current Status: The RX path is stable and handling continuous traffic.


---------------------------------
*** STM32H7 LWIP TEST *** MPU_Config: CCR before = 0x00070200 MPU_Config: CCR after = 0x00070200 DEBUG: sizeof(ETH_HandleTypeDef) = 176 DEBUG: ETH_RX_DESC_CNT (Local Define) = 4 DEBUG: offsetof(RxDescList) = 72 DEBUG: offsetof(RxDescIdx) = 92 Ethernet MAC: 00:80:e1:00:00:00 Initializing RX pool at 0x30000600... ETH: DMA Descriptors committed to hardware: RXaddr=0x30000000, RXlen=4, TXaddr=0x30000400, TXlen=4 Installing ETH interrupt handler for vector 61... ETH interrupt handler installed successfully ETH: IRQ 61 enabled in NVIC Setting MAC address... Creating RxPktSemaphore... Creating TxPktSemaphore... Registering PHY IO... LAN8742 object at: 0x0x24001ee0 IOCtx at: 0x0x240000f0 IOCtx.Init = 0x0x800defd IOCtx.DeInit = 0x0x800df11 IOCtx.WriteReg = 0x0x800df55 IOCtx.ReadReg = 0x0x800df21 IOCtx.GetTick = 0x0x800df89 Testing HAL_GetTick: 80 After 100ms delay, HAL_GetTick: 190 Creating Ethernet threads... Waiting for Ethernet link to come up... etharp_timer ethernetif_input thread started, netif=0x0x2400061c Ethernet link thread started PHY: Resetting... PHY: Enabling auto-negotiation... etharp_timer etharp_timer PHY: Link Up detected (BSR=0x782d) Negotiated: 100Mbps Full Duplex ETH: Manually initializing RX descriptors...

========== RX_ALLOCATE_CALLBACK ========== RX_ALLOC: Allocated pbuf_custom at 0x0x3000c3e0 RX_ALLOC: buff=0x0x3000c3f6 (with +2 offset), actual buff=0x0x3000c3f4 RX_ALLOC: pbuf initialized successfully ========== RX_ALLOCATE_CALLBACK END ==========

RX Init Desc 0: addr=0x3000c3f6, bkup=0x3000c3f6, len=1536, DESC3=0xc1000000

========== RX_ALLOCATE_CALLBACK ========== RX_ALLOC: Allocated pbuf_custom at 0x0x3000bdc0 RX_ALLOC: buff=0x0x3000bdd6 (with +2 offset), actual buff=0x0x3000bdd4 RX_ALLOC: pbuf initialized successfully ========== RX_ALLOCATE_CALLBACK END ==========

RX Init Desc 1: addr=0x3000bdd6, bkup=0x3000bdd6, len=1536, DESC3=0xc1000000

========== RX_ALLOCATE_CALLBACK ========== RX_ALLOC: Allocated pbuf_custom at 0x0x3000b7a0 RX_ALLOC: buff=0x0x3000b7b6 (with +2 offset), actual buff=0x0x3000b7b4 RX_ALLOC: pbuf initialized successfully ========== RX_ALLOCATE_CALLBACK END ==========

RX Init Desc 2: addr=0x3000b7b6, bkup=0x3000b7b6, len=1536, DESC3=0xc1000000

========== RX_ALLOCATE_CALLBACK ========== RX_ALLOC: Allocated pbuf_custom at 0x0x3000b180 RX_ALLOC: buff=0x0x3000b196 (with +2 offset), actual buff=0x0x3000b194 RX_ALLOC: pbuf initialized successfully ========== RX_ALLOCATE_CALLBACK END ==========

RX Init Desc 3: addr=0x3000b196, bkup=0x3000b196, len=1536, DESC3=0xc1000000 ETH: RX descriptors initialized successfully HAL_ETH_Start_IT successful ETH: Enabling DMA interrupts... ETH: DMACIER = 0x0000d0c1, MACIER = 0x00006000 ETH: DMACSR = 0x00000000 ETH: SYSCFG_PMCR = 0x03800000 ETH: MACCR = 0x3830e003 RX Desc 0: 0x3000c3f6 0x00000000 0x00000600 0xc1000000 ETH: MACCR = 0x3830e003, MACPFR = 0x80000001, MACTSCR = 0x00000000 Ethernet link is UP Interface is UP. IP: 192.168.1.10 Starting lwiperf server on port 5001... tcp_bind: bind to port 5001 Heartbeat: Total IRQs: 0, RX Cplt: 0 LLI: Single packet detected, len=60 bytes LLI: Buffer address: backup=0x3000c3f6, actual=0x3000c3f4 LLI: pbuf created successfully, p=0x0x3000c3e0, payload=0x0x3000c3f4 (Padding included) PKT DUMP (60 bytes): 0000: ff ff ff ff ff ff e8 9a 8f 8e 40 d9 08 06 00 01 0010: 08 00 06 04 00 01 e8 9a 8f 8e 40 d9 c0 a8 01 14 0020: 00 00 00 00 00 00 c0 a8 01 0a 00 00 00 00 00 00 0030: 00 00 00 00 00 00 00 00 00 00 00 00 LLI: Ethernet Header: ff:ff:ff:ff:ff:ff -> e8:9a:8f:8e:40:d9, type=0x0806

========== RX_ALLOCATE_CALLBACK ========== RX_ALLOC: Allocated pbuf_custom at 0x0x3000ab60 RX_ALLOC: buff=0x0x3000ab76 (with +2 offset), actual buff=0x0x3000ab74 RX_ALLOC: pbuf initialized successfully ========== RX_ALLOCATE_CALLBACK END ==========

LLI: Refilling descriptor 0 with new buffer 0x3000ab76 LLI: After refill - RxDescIdx=0, RxBuildDescIdx=1, RxBuildDescCnt=4 ETHERNETIF_INPUT: Got packet 62 bytes, passing to netif->input ETHERNETIF_INPUT: netif->input succeeded ethernet_input: dest:ff:ff:ff:ff:ff:ff, src:e8:9a:8f:8e:40:d9, type:806 etharp_update_arp_entry: 192.168.1.20 - e8:9a:8f:8e:40:d9 etharp_find_entry: found empty entry 0 etharp_find_entry: selecting empty entry 0 etharp_update_arp_entry: updating stable entry 0 etharp_input: incoming ARP request etharp_raw: sending raw ARP packet. ethernet_output: sending packet 0x24001f7c

========== TX START ========== TX: low_level_output called, p=0x0x24001f7c, tot_len=44 TX: Copied 44 bytes from 0x0x24001f8c to 0x0x3000d000 (Offset 0) TX DATA DUMP (44 bytes):

0000: 00 00 e8 9a 8f 8e 40 d9 00 80 e1 00 00 00 08 06 0010: 00 01 08 00 06 04 00 02 00 80 e1 00 00 00 c0 a8 0020: 01 0a e8 9a 8f 8e 40 d9 c0 a8 01 14

TX: Calling HAL_ETH_Transmit_IT (Desc Index 0) TX: Descriptor [0] State AFTER HAL Setup: DESC0 (Addr) = 0x3000d000 (Should be 0x3000d000) DESC1 = 0x00000000 DESC2 (Len) = 0x8000002c DESC3 (Ctrl) = 0x30000000 (OWN bit should be 1) TX: DMA Registers: DMACSR = 0x00000404 DMACTDTPR = 0x30000410 TX: Waiting for TxPktSemaphore... TX: TxPktSemaphore acquired TX: Descriptor [0] State AFTER DMA Completion: DESC0 (Addr) = 0x3000d000 DESC3 (Stat) = 0x30000000 (OWN bit should be 0)

========== TX_FREE_CALLBACK ========== TX_FREE: buff=0x0x24001f7c TX_FREE: pbuf freed ========== TX_FREE_CALLBACK END ==========

TX: Packet released ========== TX END ==========

========== PBUF_FREE_CUSTOM ========== PBUF_FREE: p=0x0x3000c3e0, ref=0, tot_len=46 PBUF_FREE: custom_pbuf=0x0x3000c3e0, freeing to RX_POOL ========== PBUF_FREE_CUSTOM END ==========

etharp_timer LLI: Single packet detected, len=60 bytes LLI: Buffer address: backup=0x3000bdd6, actual=0x3000bdd4 LLI: pbuf created successfully, p=0x0x3000bdc0, payload=0x0x3000bdd4 (Padding included) PKT DUMP (60 bytes): 0000: ff ff ff ff ff ff e8 9a 8f 8e 40 d9 08 06 00 01 0010: 08 00 06 04 00 01 e8 9a 8f 8e 40 d9 c0 a8 01 14 0020: 00 00 00 00 00 00 c0 a8 01 0a 00 00 00 00 00 00 0030: 00 00 00 00 00 00 00 00 00 00 00 00 LLI: Ethernet Header: ff:ff:ff:ff:ff:ff -> e8:9a:8f:8e:40:d9, type=0x0806

========== RX_ALLOCATE_CALLBACK ========== RX_ALLOC: Allocated pbuf_custom at 0x0x3000c3e0 RX_ALLOC: buff=0x0x3000c3f6 (with +2 offset), actual buff=0x0x3000c3f4 RX_ALLOC: pbuf initialized successfully ========== RX_ALLOCATE_CALLBACK END ==========

LLI: Refilling descriptor 1 with new buffer 0x3000c3f6 LLI: After refill - RxDescIdx=1, RxBuildDescIdx=2, RxBuildDescCnt=4 ETHERNETIF_INPUT: Got packet 62 bytes, passing to netif->input ETHERNETIF_INPUT: netif->input succeeded ethernet_input: dest:ff:ff:ff:ff:ff:ff, src:e8:9a:8f:8e:40:d9, type:806 etharp_update_arp_entry: 192.168.1.20 - e8:9a:8f:8e:40:d9 etharp_find_entry: found matching entry 0 etharp_update_arp_entry: updating stable entry 0 etharp_input: incoming ARP request etharp_raw: sending raw ARP packet. ethernet_output: sending packet 0x24001f7c

========== TX START ========== TX: low_level_output called, p=0x0x24001f7c, tot_len=44 TX: Copied 44 bytes from 0x0x24001f8c to 0x0x3000d000 (Offset 0) TX DATA DUMP (44 bytes):

0000: 00 00 e8 9a 8f 8e 40 d9 00 80 e1 00 00 00 08 06 0010: 00 01 08 00 06 04 00 02 00 80 e1 00 00 00 c0 a8 0020: 01 0a e8 9a 8f 8e 40 d9 c0 a8 01 14

TX: Calling HAL_ETH_Transmit_IT (Desc Index 1) TX: Descriptor [1] State AFTER HAL Setup: DESC0 (Addr) = 0x3000d000 (Should be 0x3000d000) DESC1 = 0x00000000 DESC2 (Len) = 0x8000002c DESC3 (Ctrl) = 0x30000000 (OWN bit should be 1) TX: DMA Registers: DMACSR = 0x00000404 DMACTDTPR = 0x30000420 TX: Waiting for TxPktSemaphore... TX: TxPktSemaphore acquired TX: Descriptor [1] State AFTER DMA Completion: DESC0 (Addr) = 0x3000d000 DESC3 (Stat) = 0x30000000 (OWN bit should be 0)

========== TX_FREE_CALLBACK ========== TX_FREE: buff=0x0x24001f7c TX_FREE: pbuf freed ========== TX_FREE_CALLBACK END ==========

TX: Packet released ========== TX END ==========

========== PBUF_FREE_CUSTOM ========== PBUF_FREE: p=0x0x3000bdc0, ref=0, tot_len=46 PBUF_FREE: custom_pbuf=0x0x3000bdc0, freeing to RX_POOL ========== PBUF_FREE_CUSTOM END ==========

etharp_timer Heartbeat: Total IRQs: 4, RX Cplt: 2 LLI: Single packet detected, len=60 bytes LLI: Buffer address: backup=0x3000b7b6, actual=0x3000b7b4 LLI: pbuf created successfully, p=0x0x3000b7a0, payload=0x0x3000b7b4 (Padding included) PKT DUMP (60 bytes): 0000: ff ff ff ff ff ff e8 9a 8f 8e 40 d9 08 06 00 01 0010: 08 00 06 04 00 01 e8 9a 8f 8e 40 d9 c0 a8 01 14 0020: 00 00 00 00 00 00 c0 a8 01 0a 00 00 00 00 00 00 0030: 00 00 00 00 00 00 00 00 00 00 00 00 LLI: Ethernet Header: ff:ff:ff:ff:ff:ff -> e8:9a:8f:8e:40:d9, type=0x0806

========== RX_ALLOCATE_CALLBACK ========== RX_ALLOC: Allocated pbuf_custom at 0x0x3000bdc0 RX_ALLOC: buff=0x0x3000bdd6 (with +2 offset), actual buff=0x0x3000bdd4 RX_ALLOC: pbuf initialized successfully ========== RX_ALLOCATE_CALLBACK END ==========

LLI: Refilling descriptor 2 with new buffer 0x3000bdd6 LLI: After refill - RxDescIdx=2, RxBuildDescIdx=3, RxBuildDescCnt=4 ETHERNETIF_INPUT: Got packet 62 bytes, passing to netif->input ETHERNETIF_INPUT: netif->input succeeded ethernet_input: dest:ff:ff:ff:ff:ff:ff, src:e8:9a:8f:8e:40:d9, type:806 etharp_update_arp_entry: 192.168.1.20 - e8:9a:8f:8e:40:d9 etharp_find_entry: found matching entry 0 etharp_update_arp_entry: updating stable entry 0 etharp_input: incoming ARP request etharp_raw: sending raw ARP packet. ethernet_output: sending packet 0x24001f7c

========== TX START ========== TX: low_level_output called, p=0x0x24001f7c, tot_len=44 TX: Copied 44 bytes from 0x0x24001f8c to 0x0x3000d000 (Offset 0) TX DATA DUMP (44 bytes):

0000: 00 00 e8 9a 8f 8e 40 d9 00 80 e1 00 00 00 08 06 0010: 00 01 08 00 06 04 00 02 00 80 e1 00 00 00 c0 a8 0020: 01 0a e8 9a 8f 8e 40 d9 c0 a8 01 14

TX: Calling HAL_ETH_Transmit_IT (Desc Index 2) TX: Descriptor [2] State AFTER HAL Setup: DESC0 (Addr) = 0x3000d000 (Should be 0x3000d000) DESC1 = 0x00000000 DESC2 (Len) = 0x8000002c DESC3 (Ctrl) = 0x30000000 (OWN bit should be 1) TX: DMA Registers: DMACSR = 0x00000404 DMACTDTPR = 0x30000430 TX: Waiting for TxPktSemaphore... TX: TxPktSemaphore acquired TX: Descriptor [2] State AFTER DMA Completion: DESC0 (Addr) = 0x3000d000 DESC3 (Stat) = 0x30000000 (OWN bit should be 0)

========== TX_FREE_CALLBACK ========== TX_FREE: buff=0x0x24001f7c TX_FREE: pbuf freed ========== TX_FREE_CALLBACK END ==========

TX: Packet released ========== TX END ==========

========== PBUF_FREE_CUSTOM ========== PBUF_FREE: p=0x0x3000b7a0, ref=0, tot_len=46 PBUF_FREE: custom_pbuf=0x0x3000b7a0, freeing to RX_POOL ========== PBUF_FREE_CUSTOM END ==========

etharp_timer LLI: Single packet detected, len=60 bytes LLI: Buffer address: backup=0x3000b196, actual=0x3000b194 LLI: pbuf created successfully, p=0x0x3000b180, payload=0x0x3000b194 (Padding included) PKT DUMP (60 bytes): 0000: ff ff ff ff ff ff e8 9a 8f 8e 40 d9 08 06 00 01 0010: 08 00 06 04 00 01 e8 9a 8f 8e 40 d9 c0 a8 01 14 0020: 00 00 00 00 00 00 c0 a8 01 0a 00 00 00 00 00 00 0030: 00 00 00 00 00 00 00 00 00 00 00 00 LLI: Ethernet Header: ff:ff:ff:ff:ff:ff -> e8:9a:8f:8e:40:d9, type=0x0806

========== RX_ALLOCATE_CALLBACK ========== RX_ALLOC: Allocated pbuf_custom at 0x0x3000b7a0 RX_ALLOC: buff=0x0x3000b7b6 (with +2 offset), actual buff=0x0x3000b7b4 RX_ALLOC: pbuf initialized successfully ========== RX_ALLOCATE_CALLBACK END ==========

LLI: Refilling descriptor 3 with new buffer 0x3000b7b6 LLI: After refill - RxDescIdx=3, RxBuildDescIdx=0, RxBuildDescCnt=4 ETHERNETIF_INPUT: Got packet 62 bytes, passing to netif->input ETHERNETIF_INPUT: netif->input succeeded ethernet_input: dest:ff:ff:ff:ff:ff:ff, src:e8:9a:8f:8e:40:d9, type:806 etharp_update_arp_entry: 192.168.1.20 - e8:9a:8f:8e:40:d9 etharp_find_entry: found matching entry 0 etharp_update_arp_entry: updating stable entry 0 etharp_input: incoming ARP request etharp_raw: sending raw ARP packet. ethernet_output: sending packet 0x24001f7c

========== TX START ========== TX: low_level_output called, p=0x0x24001f7c, tot_len=44 TX: Copied 44 bytes from 0x0x24001f8c to 0x0x3000d000 (Offset 0) TX DATA DUMP (44 bytes):

0000: 00 00 e8 9a 8f 8e 40 d9 00 80 e1 00 00 00 08 06 0010: 00 01 08 00 06 04 00 02 00 80 e1 00 00 00 c0 a8 0020: 01 0a e8 9a 8f 8e 40 d9 c0 a8 01 14

TX: Calling HAL_ETH_Transmit_IT (Desc Index 3) TX: Descriptor [3] State AFTER HAL Setup: DESC0 (Addr) = 0x3000d000 (Should be 0x3000d000) DESC1 = 0x00000000 DESC2 (Len) = 0x8000002c DESC3 (Ctrl) = 0x30000000 (OWN bit should be 1) TX: DMA Registers: DMACSR = 0x00000404 DMACTDTPR = 0x30000400 TX: Waiting for TxPktSemaphore... TX: TxPktSemaphore acquired TX: Descriptor [3] State AFTER DMA Completion: DESC0 (Addr) = 0x3000d000 DESC3 (Stat) = 0x30000000 (OWN bit should be 0)

========== TX_FREE_CALLBACK ========== TX_FREE: buff=0x0x24001f7c TX_FREE: pbuf freed ========== TX_FREE_CALLBACK END ==========

TX: Packet released ========== TX END ==========

========== PBUF_FREE_CUSTOM ========== PBUF_FREE: p=0x0x3000b180, ref=0, tot_len=46 PBUF_FREE: custom_pbuf=0x0x3000b180, freeing to RX_POOL ========== PBUF_FREE_CUSTOM END ==========

etharp_timer

just make a diagnostic, do not propose any fixes for now


Gemini
Responding with gemini-3-pro
Thinking
Based on the provided logs, here is the diagnostic analysis of the issue.

Diagnostic: TX Packet Corruption (Padding Issue)
The system is successfully receiving packets (RX), processing them (ARP Request), and attempting to transmit a reply (TX). However, the transmitted packets are malformed, causing the remote host (PC) to ignore them.

1. Evidence of Success (RX & Logic)
RX Works: The log shows RX Desc 0, 1, 2, 3 receiving packets sequentially.
Logic Works: The stack identifies the ARP Request (Who has 192.168.1.10?) and generates an ARP Reply (192.168.1.10 is at 00:80:e1...).
TX Hardware Works: The DMA completes the transfer (TX: Descriptor [x] State AFTER DMA Completion: ... OWN bit should be 0), and the semaphore is acquired.
2. The Problem: Malformed TX Packet
The transmitted packet contains 2 extra bytes of zeros at the beginning. This shifts the entire Ethernet frame, making the Destination MAC Address incorrect on the wire.

From the Log:

text
TX DATA DUMP (44 bytes):
  0000: 00 00 e8 9a 8f 8e 40 d9 00 80 e1 00 00 00 08 06 ...
What is sent: 00 00 e8 9a 8f 8e ...
What should be sent: e8 9a 8f 8e 40 d9 ... (Destination MAC)
Because of the leading 00 00, the receiving device sees the Destination MAC as 00:00:e8:9a:8f:8e instead of its own MAC (e8:9a:8f:8e:40:d9). Consequently, it drops the packet.

3. Root Cause: ETH_PAD_SIZE
It appears ETH_PAD_SIZE is defined as 2 in your lwipopts.h.

In RX: You correctly handle this by manually adjusting the payload pointer: p->payload = (uint8_t *)p->payload + 2;.
In TX: LwIP's ethernet_output function automatically adds ETH_PAD_SIZE bytes to the pbuf payload pointer before calling your low_level_output. This is intended to allow drivers to align data or insert headers.
The Failure: Your low_level_output blindly copies the entire pbuf (including the 2 padding bytes) into the transmit bounce buffer.