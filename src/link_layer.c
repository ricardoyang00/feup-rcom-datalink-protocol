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

int timeout = 0;
int retransmitions = 0;

int tramaTransmitter = 0;
int tramaReceiver = 1;

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
    if (openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate) < 0) return -1;

    LinkLayerState state = START_STATE;
    unsigned char byte;

    timeout = connectionParameters.timeout;
    retransmissions = connectionParameters.retransmissions;

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
                            if (byte == A_R) state = A_RCV;
                            else if (byte != FLAG) state = START_STATE;
                            break;
                        case A_RCV:
                            if (byte == C_UA) state = C_RCV;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START_STATE;
                            break;
                        case C_RCV:
                            if (byte == FLAG) state = FLAG_RCV;
                            else if ((A_R ^ C_UA) == byte) state = BCC_OK;
                            else state = START_STATE;
                            break;
                        case BCC_OK:
                            if (byte == FLAG) state = STOP_STATE;
                            else state = START_STATE;
                            break;
                        default:
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
                            if (byte == A_T) state = A_RCV;
                            else if (byte != FLAG) state = START_STATE;
                            break;
                        case A_RCV:
                            if (byte == C_SET) state = C_RCV;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START_STATE;
                            break;
                        case C_RCV:
                            if (byte == FLAG) state = FLAG_RCV;
                            else if ((A_T ^ C_SET) == byte) state = BCC_OK;
                            else state = START_STATE;
                            break;
                        case BCC_OK:
                            if (byte == FLAG) state = STOP_STATE;
                            else state = START_STATE;
                            break;
                        default:
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
    int frameSize = bufSize + 6;
    unsigned char *frame = malloc(frameSize);
    frame[0] = FLAG;
    frame[1] = A_T;
    frame[2] = C_N(tramaTransmitter);
    frame[3] = frame[1] ^ frame[2];

    memcpy(frame + 4, buf, bufSize);

    unsigned char BCC2 = buf[0];
    for (int i = 1; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }

    int currentFrameIndex = 4;
    for (int i = 0; i < bufSize; i++) {
        switch (buf[i]) {
            case FLAG:
                frame = realloc(frame, ++frameSize);
                frame[currentFrameIndex++] = ESC;
                frame[currentFrameIndex++] = SUF_FLAG;
                break;
            case ESC:
                frame = realloc(frame,++frameSize);
                frame[currentFrameIndex++] = ESC;
                frame[currentFrameIndex++] = SUF_ESC;
                break;
            default:
                frame[currentFrameIndex++] = buf[i];
                break;
        }
    }

    frame[currentFrameIndex++] = BCC2;
    frame[currentFrameIndex] = FLAG;

    int isAccepted = FALSE, isRejected = FALSE;
    int alarmCount = 0;
    (void) signal(SIGALRM, alarmHandler);

    alarmEnabled = FALSE;

    while (alarmCount < retransmissions) {
        isAccepted = FALSE;
        isRejected = FALSE;

        alarm(timeout);
        alarmEnabled = TRUE;

        while (alarmEnabled && !isAccepted && !isRejected) {
            writeBytesSerialPort(frame, ++currentFrameIndex);
            
            unsigned char byte_C = 0;
            unsigned char byte;
            LinkLayerState state = START_STATE;

            while (state != STOP_STATE) {
                if (readByteSerialPort(&byte) <= 0) continue;
                
                switch (state) {
                    case START_STATE:
                        if (byte == FLAG) state = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (byte == A_R) state = A_RCV;
                        else if (byte != FLAG) state = START_STATE;
                        break;
                    case A_RCV:
                        if (byte == RR0 || byte == RR1 || byte == REJ0 || byte == REJ1) {
                            state = C_RCV;
                            tempByte_A = byte;

                            if (byte == RR0 || byte == RR1) isAccepted = TRUE;
                            else isRejected = TRUE;
                        }
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START_STATE;
                        break;
                    case C_RCV:
                        if (byte == FLAG) state = FLAG_RCV;
                        else if ((A_R ^ tempByte_A) == byte) state = BCC_OK;
                        else state = START_STATE;
                        break;
                    case BCC_OK:
                        if (byte == FLAG) state = STOP_STATE;
                        else state = START_STATE;
                        break;
                    default:
                        break;
                }
            }
        }

        if (isAccepted) break;
        
    }

    free(frame);
    if (isAccepted) return frameSize;
        
    return -1;
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
