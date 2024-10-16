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
    printf("Alarm #%d\n", alarmCount + 1);
    
    alarmEnabled = FALSE;
    alarmCount++;
}


////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {
    if (openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate) < 0) {
        return -1;
    }

    int fd = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
    if (fd < 0) return -1;

    LinkLayerState state = START_STATE;
    unsigned char checkBuffer[2] = {0};

    unsigned char byte;

    switch (connectionParameters.role) {
        case LlTx: {
            
            alarmCount = 0;
            (void) signal(SIGALRM, alarmHandler);

            while (alarmCount < connectionParameters.nRetransmissions) {

                if (sendSVF(A_T, C_SET) < 0) return -1;
                printf("TR: Sent SET Buffer Sucessfully\n");

                alarm(connectionParameters.timeout);
                alarmEnabled = TRUE;
                
                while (state != STOP_STATE && alarmEnabled) {
                    if (readByteSerialPort(&byte) <= 0) continue;
                    
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

                if (state == STOP_STATE) {
                    printf("TR: Received UA Buffer Sucessfully\n");
                    break;
                }
                
            }

            if (state != STOP_STATE) return -1;
            break; 
        }
        case LlRx:

            while (state != STOP_STATE) {
                if (readByteSerialPort(&byte) <= 0) continue;
                
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

            printf("RCV: Received SET buffer Sucessfully\n");
            
            if (sendSVF(A_R, C_UA) < 0) return -1;

            printf("RCV: Sent UA buffer Sucessfully\n");

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

int sendSVF(unsigned char A, unsigned char C) {
    unsigned char buf_T[5] = {FLAG, A, C, A ^ C, FLAG};

    return (writeBytesSerialPort(buf_T, 5) < 0);
}
