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

## Why we are POSITIVE it is NOT a Race Condition
We performed a specific "Stress Test" to rule out cache coherency, write buffering, or bus timing issues:
1.  We manually invalidated D-Cache for the descriptor region.
2.  We placed a `__DSB()` (Data Synchronization Barrier) instruction.
3.  We inserted a **Hardware Delay** (volatile loop of ~500,000 NOPs) *between* setting the Descriptor's OWN bit and Kicking the DMA.
4.  **Result**: The RBU loop persisted exactly as before.
    - If it were a race condition (DMA reading RAM before CPU write finished), the 500k cycle delay would have fixed it.
    - Conclusion: The DMA is looking at the correct memory values, but **refusing to use them**.

## Why we suspect Memory Allocation / Layout
During debugging, we encountered a flaw where the ST HAL library was hardcoded to 4 descriptors (`ETH_RX_DESC_CNT`), but we defined 16 in our header. This caused a buffer overflow corruption.
- We reverted to 4 descriptors.
- We manually spaced the tables: RX at offset `0x0`, TX at `0x400`, Buffers at `0x600`.
- The issue persists.

## The Ask
Given that **Timing/Cache is ruled out** and the **Descriptor bytes are valid** (`0xC1000000`), what obscure STM32H7 DMA mechanism causes it to continuously reject a valid descriptor?
- Is there an alignment requirement for the *buffers* that is stricter than 32 bytes?
- Is "Ring Mode" requiring a specific bits in `DESC1` (Control)?
- Is the `DMACRDTPR` (Tail Pointer) calculation logic of `(ReadIdx - 1)` somehow invalid when the ring is small (4 items)?
- Does the "Receive Poll Demand" (`DMARPDR`) require a specific sequence relative to the RBU clear?

Please analyze based on the assumption that **Memory Layout or Descriptor Content** is the root cause, not timing.
