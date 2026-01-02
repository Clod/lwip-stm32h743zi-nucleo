#!/usr/bin/env python3
"""
STM32H7 Network Stack Regression Test Suite
=============================================
Comprehensive testing for UDP, ICMP, and TCP functionality
with focus on alignment-critical small packet handling.

Author: Antigravity AI Assistant
Date: 2026-01-02
"""

import socket
import subprocess
import time
import sys
import argparse
from typing import Tuple, Optional

# ANSI color codes
class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    BOLD = '\033[1m'
    RESET = '\033[0m'

class TestResult:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.errors = []
    
    def add_pass(self, name: str, details: str = ""):
        self.passed += 1
        print(f"{Colors.GREEN}[PASS]{Colors.RESET} {name}: {details}")
    
    def add_fail(self, name: str, details: str = ""):
        self.failed += 1
        self.errors.append(f"{name}: {details}")
        print(f"{Colors.RED}[FAIL]{Colors.RESET} {name}: {details}")
    
    def summary(self):
        total = self.passed + self.failed
        print(f"\n{Colors.BOLD}Test Summary{Colors.RESET}")
        print("=" * 50)
        print(f"Total: {total} | Passed: {Colors.GREEN}{self.passed}{Colors.RESET} | Failed: {Colors.RED}{self.failed}{Colors.RESET}")
        
        if self.failed > 0:
            print(f"\n{Colors.RED}Failed Tests:{Colors.RESET}")
            for error in self.errors:
                print(f"  - {error}")
            print(f"\n{Colors.BOLD}Status: {Colors.RED}❌ REGRESSION TEST FAILED{Colors.RESET}")
            return False
        else:
            print(f"\n{Colors.BOLD}Status: {Colors.GREEN}✅ ALL TESTS PASSED{Colors.RESET}")
            return True


def udp_echo_test(board_ip: str, port: int, payload: str, test_name: str, result: TestResult, timeout: float = 2.0) -> bool:
    """
    Send UDP packet and verify echoed response is reversed with \r\n appended.
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        
        # Send payload
        start_time = time.time()
        sock.sendto(payload.encode('utf-8'), (board_ip, port))
        
        # Receive response
        data, addr = sock.recvfrom(1024)
        elapsed_ms = int((time.time() - start_time) * 1000)
        sock.close()
        
        # Verify response
        expected = payload[::-1] + "\r\n"  # Reversed + CRLF
        actual = data.decode('utf-8')
        
        if actual == expected:
            result.add_pass(test_name, f'"{payload}" → "{actual.strip()}" ({elapsed_ms}ms)')
            return True
        else:
            result.add_fail(test_name, f'Expected "{expected}", got "{actual}"')
            return False
            
    except socket.timeout:
        result.add_fail(test_name, f"Timeout waiting for response")
        return False
    except Exception as e:
        result.add_fail(test_name, f"Exception: {str(e)}")
        return False


def ping_test(board_ip: str, count: int, test_name: str, result: TestResult) -> bool:
    """
    Perform ICMP ping test.
    """
    try:
        cmd = ["ping", "-c", str(count), "-W", "2", board_ip]
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        
        if proc.returncode == 0:
            # Parse ping output for stats
            output = proc.stdout
            if "0% packet loss" in output:
                # Extract RTT if available
                rtt_line = [line for line in output.split('\n') if 'rtt' in line or 'round-trip' in line]
                rtt_info = rtt_line[0].split('=')[-1].strip() if rtt_line else "N/A"
                result.add_pass(test_name, f"{count}/{count} packets, {rtt_info}")
                return True
            else:
                result.add_fail(test_name, "Packet loss detected")
                return False
        else:
            result.add_fail(test_name, "Ping command failed")
            return False
            
    except subprocess.TimeoutExpired:
        result.add_fail(test_name, "Ping timeout")
        return False
    except Exception as e:
        result.add_fail(test_name, f"Exception: {str(e)}")
        return False


def iperf_test(board_ip: str, duration: int, test_name: str, result: TestResult) -> bool:
    """
    Perform iperf TCP throughput test.
    """
    try:
        cmd = ["iperf", "-c", board_ip, "-t", str(duration), "-f", "m"]
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=duration + 10)
        
        if proc.returncode == 0:
            output = proc.stdout
            # Parse for bandwidth
            lines = output.split('\n')
            for line in lines:
                if 'Mbits/sec' in line and 'sender' not in line.lower():
                    parts = line.split()
                    for i, part in enumerate(parts):
                        if 'Mbits/sec' in part and i > 0:
                            bandwidth = float(parts[i-1])
                            if bandwidth >= 1.5:
                                result.add_pass(test_name, f"{bandwidth:.2f} Mbps")
                                return True
                            else:
                                result.add_fail(test_name, f"Low bandwidth: {bandwidth:.2f} Mbps")
                                return False
            result.add_fail(test_name, "Could not parse iperf output")
            return False
        else:
            result.add_fail(test_name, "iperf connection failed")
            return False
            
    except subprocess.TimeoutExpired:
        result.add_fail(test_name, "iperf timeout")
        return False
    except FileNotFoundError:
        result.add_fail(test_name, "iperf not installed (skipping)")
        return True  # Don't fail if iperf unavailable
    except Exception as e:
        result.add_fail(test_name, f"Exception: {str(e)}")
        return False


def main():
    parser = argparse.ArgumentParser(description='STM32H7 Network Regression Tests')
    parser.add_argument('--board-ip', default='192.168.1.10', help='Board IP address')
    parser.add_argument('--udp-port', type=int, default=5005, help='UDP echo server port')
    parser.add_argument('--skip-iperf', action='store_true', help='Skip iperf test')
    parser.add_argument('--quick', action='store_true', help='Run quick test subset')
    args = parser.parse_args()
    
    print(f"{Colors.BOLD}{Colors.BLUE}STM32H7 Network Regression Test Suite{Colors.RESET}")
    print("=" * 50)
    print(f"Board IP: {args.board_ip}")
    print(f"UDP Port: {args.udp_port}")
    print()
    
    result = TestResult()
    
    # Test Suite
    print(f"{Colors.BOLD}UDP Echo Tests (Critical Alignment Cases){Colors.RESET}")
    print("-" * 50)
    
    # Critical small packet tests (previously caused crashes)
    udp_tests = [
        ("UDP-Tiny-1", "A"),
        ("UDP-Tiny-2", "AB"),
        ("UDP-Tiny-3", "CFS"),
        ("UDP-Tiny-4", "NASA"),
        ("UDP-Small-5", "RTEMS"),
        ("UDP-Small-7", "RTEMS61"),
        ("UDP-Small-8", "neuquen1"),
    ]
    
    if not args.quick:
        udp_tests.extend([
            ("UDP-Medium-16", "0123456789ABCDEF"),
            ("UDP-Medium-32", "A" * 32),
            ("UDP-Large-64", "B" * 64),
            ("UDP-Large-128", "C" * 128),
        ])
    
    for test_name, payload in udp_tests:
        udp_echo_test(args.board_ip, args.udp_port, payload, test_name, result)
        time.sleep(0.1)  # Small delay between tests
    
    print()
    print(f"{Colors.BOLD}ICMP Tests{Colors.RESET}")
    print("-" * 50)
    ping_test(args.board_ip, 5, "Ping-Basic", result)
    
    if not args.skip_iperf:
        print()
        print(f"{Colors.BOLD}TCP Tests{Colors.RESET}")
        print("-" * 50)
        iperf_test(args.board_ip, 5 if args.quick else 10, "iperf-RX", result)
    
    print()
    success = result.summary()
    
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
