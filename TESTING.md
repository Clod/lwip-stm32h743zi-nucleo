# Testing Instructions for STM32H743ZI Nucleo

This guide explains how to test the LwIP network stack on the STM32H743ZI Nucleo board.

## Prerequisites

### Hardware
1.  **STM32H743ZI Nucleo-144 Board**.
2.  **Ethernet Cable**: Connected to the **RJ45 port** on the board.
3.  **Micro-USB Cable**: Connected to the **ST-LINK USB port** (for flashing and serial console).
4.  **Network Setup**: A host PC connected to the same network as the board.

### Software
1.  **Iperf 2.0.x**: The ST examples specifically recommend version 2.0.x. Newer versions (Iperf 3.x) are **not compatible**.
2.  **Terminal Emulator**: (e.g., PuTTY, TeraTerm, or `screen`) to view the serial output.

---

## 1. Network Configuration

The test application (`stm32h7_test.exe`) is configured with the following static IP settings:

| Parameter | Value |
| :--- | :--- |
| **IP Address** | `192.168.0.10` |
| **Netmask** | `255.255.255.0` |
| **Gateway** | `192.168.0.1` |

**Note**: Ensure your host PC is on the same subnet (e.g., set your PC to `192.168.0.20`).

---

## 2. Build and Flash

### Build the project
Run the following commands in the root directory:
```bash
./waf configure --rtems-bsps=arm/nucleo-h743zi --rtems=/opt/rtems/6.1 --rtems-version=6
./waf
```

### Flash the board
The binary is located at:
`build/arm-rtems6-nucleo-h743zi/stm32h7_test.exe`

You can use `OpenOCD`, `STM32CubeProgrammer`, or simply copy the `.bin` (if converted) to the Nucleo's virtual USB drive.

---

## 3. Running the Test

1.  **Connect Console**: Open your serial terminal (115200 baud, 8N1) associated with the Nucleo ST-LINK USB.
2.  **Reset Board**: Press the **Black Reset button** on the board.
3.  **Verify Output**: You should see:
    ```text
    *** STM32H7 LWIP TEST ***
    Interface up, starting lwiperf server...
    lwiperf server started on port 5001
    ```
4.  **Ping Test**: From your PC, verify connectivity:
    ```bash
    ping 192.168.1.10
    ```
5.  **Performance Test (Iperf)**:
    Run the iperf client from your PC to measure throughput:
    ```bash
    iperf -c 192.168.1.10 -p 5001
    ```

---

## Interface Usage Clarification

*   **Ethernet Port (RJ45)**: Used for all network traffic and testing logic.
*   **USB Port (ST-LINK)**: Used for power, flashing the firmware, and viewing the RTEMS diagnostic console (Serial-over-USB).
