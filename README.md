# Data Link Protocol
This project was developed as part of the Redes e Computadores (RCOM) course to develop a data link protocol to enable file transfer between two computers connected via an RS-232 serial cable, by implementing both transmitter and receiver functionalities.

## Project Structure

- **bin/**: Compiled binaries.
- **src/**: Source code for the implementation of the link-layer and application layer protocols.
- **include/**: Header files for the link-layer and application layer protocols.
- **cable/**: Virtual cable program to help test the serial port. This file must not be changed.
- **main.c**: Main file.
- **Makefile**: Makefile to build the project and run the application.
- **penguin.gif**: Example file to be sent through the serial port.

---

## Quick Start

> [!IMPORTANT]  
> For **macOS** users, please read [this guide](cable/macos-cable.md).

1. **Compile the Application and Virtual Cable Program**
   - Use the provided Makefile to compile the project:
     ```sh
     make
     ```

2. **Run the Virtual Cable Program**
   - You can run the virtual cable program manually or using the Makefile target:
     ```sh
     sudo ./bin/cable_app
     ```
     ```sh
     sudo make run_cable
     ```

3. **Test the Protocol Without Cable Disconnections and Noise**

   3.1 **Run the Receiver**
   - Run the receiver manually or using the Makefile target:
     ```sh
     ./bin/main /dev/ttyS11 9600 rx penguin-received.gif
     ```
     ```sh
     make run_rx
     ```

   3.2 **Run the Transmitter**
   - Run the transmitter manually or using the Makefile target:
     ```sh
     ./bin/main /dev/ttyS10 9600 tx penguin.gif
     ```
     ```sh
     make run_tx
     ```

   3.3 **Verify the Received File**
   - Check if the received file matches the sent file using the `diff` command or the Makefile target:
     ```sh
     diff -s penguin.gif penguin-received.gif
     ```
     ```sh
     make check_files
     ```

4. **Test the Protocol With Cable Disconnections and Noise**

   4.1 **Run the Receiver and Transmitter Again**
   - Follow the same steps as in section 3 to run the receiver and transmitter.

   4.2 **Simulate Cable Disconnections and Noise**
   - Quickly switch to the cable program console and use the following commands:
     - `off`: Simulate unplugging the cable.
     - `ber <value 0-1>`: Add noise with a specified bit error rate.
     - `ber 0`: Return to normal operation.

   4.3 **Verify the Received File**
   - Check if the received file matches the sent file, even with cable disconnections or noise, using the `diff` command or the Makefile target:
     ```sh
     diff -s penguin.gif penguin-received.gif
     ```
     ```sh
     make check_files
     ```

## Statistics and Report

For detailed report, click [here](docs/RCOM-Data-Link-Protocol-report-Final.pdf).
