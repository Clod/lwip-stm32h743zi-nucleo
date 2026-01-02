# STM32H7 Network Regression Test Suite

## Overview
Automated test suite to verify the stability of the STM32H7 LwIP networking stack, with special focus on UDP alignment-critical small packet handling.

## Quick Start

### Prerequisites
1. STM32H743ZI board flashed with `stm32h7_test_TRULY_BULLETPROOF.elf`
2. Board connected to network with IP `192.168.1.10`
3. Python 3.x installed on host machine
4. Network connectivity between host and board

### Running Tests

**Full Test Suite** (recommended):
```bash
cd /home/utndev/src/lw-ip-stm-32-h-743-zi-nucleo/test
python3 regression_test.py
```

**Quick Test** (subset for rapid validation):
```bash
python3 regression_test.py --quick
```

**Custom Board IP**:
```bash
python3 regression_test.py --board-ip 192.168.1.20
```

**Skip iperf** (if not installed):
```bash
python3 regression_test.py --skip-iperf
```

## Test Coverage

### UDP Echo Tests (Critical)
Tests the byte-by-byte memcpy fix with various packet sizes:
- **Tiny packets (1-4 bytes)**: Previously caused Hard Faults
- **Small packets (5-8 bytes)**: Alignment edge cases
- **Medium packets (16-32 bytes)**: Normal operation
- **Large packets (64-128 bytes)**: Buffer capacity (full suite only)

### ICMP Tests
- Basic ping: 5 packets with default size
- Verifies network layer stability

### TCP Tests
- iperf throughput test (5-10 seconds)
- Minimum expected: 1.5 Mbps
- Requires `iperf` installed on host

## Expected Output

```
STM32H7 Network Regression Test Suite
==================================================
Board IP: 192.168.1.10
UDP Port: 5005

UDP Echo Tests (Critical Alignment Cases)
--------------------------------------------------
[PASS] UDP-Tiny-1: "A" → "A" (2ms)
[PASS] UDP-Tiny-2: "AB" → "BA" (1ms)
[PASS] UDP-Tiny-3: "CFS" → "SFC" (2ms)
[PASS] UDP-Tiny-4: "NASA" → "ASAN" (1ms)
[PASS] UDP-Small-5: "RTEMS" → "SMETR" (2ms)
[PASS] UDP-Small-7: "RTEMS61" → "16SMETR" (1ms)
[PASS] UDP-Small-8: "neuquen1" → "1neuquen" (2ms)

ICMP Tests
--------------------------------------------------
[PASS] Ping-Basic: 5/5 packets, min/avg/max = 0.2/0.8/1.5 ms

TCP Tests
--------------------------------------------------
[PASS] iperf-RX: 2.16 Mbps

Test Summary
==================================================
Total: 9 | Passed: 9 | Failed: 0

Status: ✅ ALL TESTS PASSED
```

## Troubleshooting

### "Connection timed out" / "No response"
- **Cause**: Board not running or network issue
- **Fix**: Verify board is powered, running firmware, and network cable connected
- **Check**: Can you ping `192.168.1.10`?

### "iperf not installed"
- **Cause**: iperf package not available on host
- **Fix**: Install with `sudo apt-get install iperf` (Debian/Ubuntu) or use `--skip-iperf`

### Intermittent Failures
- **Cause**: Network congestion or timing issues
- **Fix**: Re-run test; consistent failures indicate a real problem

### All UDP Tests Fail
- **Cause**: Wrong port or UDP server not running
- **Fix**: Verify board console shows "UDP Server: Listening on port 5005..."

## Interpreting Results

### Critical Tests
The following tests MUST pass for the fix to be considered stable:
- `UDP-Tiny-3` (CFS) - 3-byte packet
- `UDP-Tiny-4` (NASA) - 4-byte packet
- These previously caused Hard Faults before the bulletproof fix

### Non-Critical
- `iperf-RX` may vary based on network conditions (1.5-3.0 Mbps typical)
- Ping RTT will vary based on switch/router

## Adding New Tests

Edit `regression_test.py` and add to the `udp_tests` list:
```python
udp_tests.extend([
    ("My-Test", "MyPayload"),
])
```

## Continuous Integration
This test suite can be integrated into CI/CD pipelines:
```bash
python3 regression_test.py --quick && echo "CI PASS" || echo "CI FAIL"
```

Exit code: 0 = success, 1 = failure

## Test History
- **2026-01-02**: Initial regression suite created
- **Focus**: Unaligned access fix verification

## Support
For issues or questions, refer to the main project walkthrough at:
`/home/utndev/.gemini/antigravity/brain/.../walkthrough.md`
