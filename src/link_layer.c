// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>


// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

////////////////////////////////////////////////
// ALARM RELATED FUNCTIONS
////////////////////////////////////////////////
int alarmEnabled = FALSE;
int alarmCount = 0;

// Alarm function handler
void alarmHandler(int signal)
{
    printf("Alarm #%d\n", alarmCount);
    
    alarmEnabled = FALSE;
    alarmCount++;
}


////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {
    printf("open\n");
    if (openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate) < 0) {
        return -1;
    }
    printf("open\n");
    int fd = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
    printf("%d\n", fd);
    if (fd < 0) return -1;

    LinkLayerState state = START_STATE;
    unsigned char checkBuffer[2] = {0};

    unsigned char byte;

    switch (connectionParameters.role) {
        case LlTx: {
            
            alarmCount = 0;
            (void) signal(SIGALRM, alarmHandler);

            while (alarmCount < connectionParameters.nRetransmissions) {
                unsigned char buf_T[5] = {FLAG, A_T, C_SET, A_T ^ C_SET, FLAG};
                int bufSize = 5;
                if (write(fd, buf_T, bufSize) < 0) return -1;

                alarm(connectionParameters.timeout);
                alarmEnabled = TRUE;
                
                while (state != STOP_STATE && alarmEnabled) {
                    if (read(fd, &byte, 1) <= 0) continue;
                    
                    switch (state) {
                        case START_STATE:
                            if (byte == FLAG) state = FLAG_RCV;
                            break;
                        case FLAG_RCV:
                            if (byte == A_R) {
                                state = A_RCV;
                                checkBuffer[0] = byte;
                            }
                            else if (byte != FLAG) state = START_STATE;
                            break;
                        case A_RCV:
                            if (byte == C_UA) {
                                state = C_RCV;
                                checkBuffer[1] = byte;
                            }
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START_STATE;
                            break;
                        case C_RCV:
                            if (byte == FLAG) state = FLAG_RCV;
                            else if ((checkBuffer[0] ^ checkBuffer[1]) == byte) state = BCC_OK;
                            else state = START_STATE;
                            break;
                        case BCC_OK:
                            if (byte == FLAG) state = STOP_STATE;
                            else state = START_STATE;
                            break;
                        default:
                            state = START_STATE;
                            break;
                    }
                }
            }
            if (state != STOP_STATE) return -1;
            break; 
        }
        case LlRx:

            while (state != STOP_STATE) {
                if (read(fd, &byte, 1) <= 0) continue;
                
                switch (state) {
                        case START_STATE:
                            if (byte == FLAG) state = FLAG_RCV;
                            break;
                        case FLAG_RCV:
                            if (byte == A_T) {
                                state = A_RCV;
                                checkBuffer[0] = byte;
                            }
                            else if (byte != FLAG) state = START_STATE;
                            break;
                        case A_RCV:
                            if (byte == C_SET) {
                                state = C_RCV;
                                checkBuffer[1] = byte;
                            }
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START_STATE;
                            break;
                        case C_RCV:
                            if (byte == FLAG) state = FLAG_RCV;
                            else if ((checkBuffer[0] ^ checkBuffer[1]) == byte) state = BCC_OK;
                            else state = START_STATE;
                            break;
                        case BCC_OK:
                            if (byte == FLAG) state = STOP_STATE;
                            else state = START_STATE;
                            break;
                        default:
                            state = START_STATE;
                            break;
                    }
            }

            unsigned char buf_R[5] = {FLAG, A_R, C_UA, A_R ^ C_UA, FLAG};
            int bufSize = 5;
            if (write(fd, buf_R, bufSize) < 0) return -1;

            break;
    }

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // TODO

    int clstat = closeSerialPort();
    return clstat;
}
