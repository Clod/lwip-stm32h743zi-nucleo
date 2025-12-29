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
