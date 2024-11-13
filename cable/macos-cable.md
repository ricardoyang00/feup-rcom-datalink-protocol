# Simulating a Serial Cable using `socat` on macOS

Since `cable.c` uses `sched_setscheduler`, which is only available on Linux, we need an alternative for macOS. This guide explains how to use `socat` to simulate a serial cable on macOS.

**Testing Environment**: macOS Sonoma Version 14.4

## Instructions

1. **Install `socat`**
   ```sh
   brew install socat
   ```

2. **Create a Pair of Virtual Serial Ports (PTYs)**
   ```sh
   socat -d -d pty,raw,echo=0 pty,raw,echo=0
   ```

   You will see output similar to this:
   ```sh
   2024/01/01 00:00:00 socat[7909] N PTY is /dev/ttys009
   2024/01/01 00:00:00 socat[7909] N PTY is /dev/ttys010
   ```

   You can now use `/dev/ttys009` and `/dev/ttys010` as your receiver and transmitter ports, allowing communication between them.

## Constraints

- **Unlimited Speed**: The virtual serial ports do not simulate the real speed limitations of a physical serial port.
- **Disconnection Simulation**: Simulating disconnections is tough because they occur very quickly.
- **Noise Simulation**: It is not possible to simulate noise.

Due to these constraints, this setup is recommended for testing the normal usage of the protocol. For more detailed testing, it is recommended to use a lightweight VM with Linux.