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
#include <signal.h>
#include <time.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

void alarmHandler(int signal);

int alarmEnabled = FALSE;
int alarmCount = 0;
int retransmissions = 0;
int timeout = 0;

int tramaRx = 0;
int tramaTx = 1;

LinkLayerStatistics statistics = {
    .errors = 0,
    .framesSent = 0,
    .framesReceived = 0,
    .propagationDelay = 0.0
};

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {
    if (openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate) < 0) return -1;

    LinkLayerState state = START_STATE;
    unsigned char byte;

    timeout = connectionParameters.timeout;
    retransmissions = connectionParameters.nRetransmissions;

    switch (connectionParameters.role) {
        case LlTx: 
            alarmCount = 0;
            (void) signal(SIGALRM, alarmHandler);

            while (alarmCount < connectionParameters.nRetransmissions) {
                if (sendSVF(A_T, C_SET) < 0) return -1;
                printf("TR: Set Frame Sent\n");

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
                    printf("TR: Received UA Frame\n");
                    break;
                }
            }

            if (state != STOP_STATE) {
                printf("Tries exceeded\n");
                return -1;
            }

            break; 

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

            printf("RCV: Received Set Frame\n");

            if (sendSVF(A_R, C_UA) < 0) return -1;
            
            printf("RCV: Sent UA Frame\n");

            break;
    }

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {   
    int frameSize = bufSize + 6;
    unsigned char *frame = malloc(frameSize);

    frame[0] = FLAG;
    frame[1] = A_T;
    frame[2] = C_N(tramaRx);
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

    LinkLayerState state = START_STATE;

    while (alarmCount < retransmissions) {
        isAccepted = FALSE;
        isRejected = FALSE;

        alarm(timeout);
        alarmEnabled = TRUE;

        clock_t start, end;
    
        while (alarmEnabled && !isAccepted && !isRejected) {
            writeBytesSerialPort(frame, ++currentFrameIndex);
            
            unsigned char byte_C = 0;
            unsigned char byte;

            while (state != STOP_STATE) {
                start = clock();

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
                        if (byte == C_RR(0) || byte == C_RR(1) || byte == C_REJ(0) || byte == C_REJ(1)) {
                            state = C_RCV;
                            byte_C = byte;

                            if (byte == C_RR(0) || byte == C_RR(1)) {
                                isAccepted = TRUE;
                                nextTramaTx();
                            } else {
                                isRejected = TRUE;
                                incrementErrors();
                            }
                        }
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START_STATE;
                        break;

                    case C_RCV:
                        if (byte == FLAG) state = FLAG_RCV;
                        else if ((A_R ^ byte_C) == byte) state = BCC_OK;
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

            end = clock();
            double delay = (double) (end - start) / CLOCKS_PER_SEC;
            addPropagationDelay(delay);
        }
        
        if (isAccepted) break;
    }

    if (state != STOP_STATE) {
        printf("Tries exceeded\n");
    }

    free(frame);

    if (isAccepted) return frameSize;
    
    llclose(FALSE);
    return -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
    unsigned char byte_C;
    unsigned char byte;

    LinkLayerState state = START_STATE;

    int currentFrameIndex = 0;
    
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
                if (byte == C_N(0) || byte == C_N(1)) {
                    state = C_RCV;
                    byte_C = byte;
                }
                else if (byte == FLAG) state = FLAG_RCV;
                else if (byte == C_DISC) {
                    if (sendSVF(A_R, C_DISC) < 0) return -1;
                    return 0;
                }
                else state = START_STATE;
                break;

            case C_RCV:
                if (byte == FLAG) state = FLAG_RCV;
                else if ((A_T ^ byte_C) == byte) state = DATA_STATE;
                else state = START_STATE;
                break;

            case DATA_STATE:
                if (byte == ESC) state = ESC_STATE;
                else if (byte == FLAG) {
                    unsigned char BCC2 = packet[currentFrameIndex - 1];
                    currentFrameIndex--;
                    packet[currentFrameIndex] = '\0';
                    
                    unsigned char xor = packet[0];
                    for (int i = 1; i < currentFrameIndex; i++) {
                        xor ^= packet[i];
                    }

                    if (xor == BCC2) {
                        if (sendSVF(A_R, C_RR(tramaRx)) < 0) return -1;
                        nextTramaRx();
                        return currentFrameIndex;
                    } else {
                        if (sendSVF(A_R, C_REJ(tramaRx)) < 0) return -1;
                        printf("RCV Error: BCC2 does not match\n");
                        state = START_STATE;
                        break;
                    }
                }
                else packet[currentFrameIndex++] = byte;
                break;

            case ESC_STATE:
                if (byte == SUF_FLAG) packet[currentFrameIndex++] = FLAG;
                else if (byte == SUF_ESC) packet[currentFrameIndex++] = ESC;
                else packet[currentFrameIndex++] = byte;
                state = DATA_STATE;
                break;

            default:
                break;        
        }
    }

    return -1;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics) {
    unsigned char byte;
    LinkLayerState state = START_STATE;

    (void) signal(SIGALRM, alarmHandler);

    alarmCount = 0;

    while (alarmCount < retransmissions) {
        if (sendSVF(A_T, C_DISC) < 0) return -1;
        incrementFramesSent();

        alarm(timeout);
        alarmEnabled = TRUE;

        clock_t start, end;
        
        while (state != STOP_STATE && alarmEnabled) {
            start = clock();

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
                    if (byte == C_DISC) state = C_RCV;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START_STATE;
                    break;

                case C_RCV:
                    if (byte == (A_R ^ C_DISC)) state = BCC_OK;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START_STATE;
                    break;

                case BCC_OK:
                    if (byte == FLAG) state = STOP_STATE;
                    else state = START_STATE;
                    break;

                default:
                    break;
            }

            end = clock();
            double delay = (double) (end - start) / CLOCKS_PER_SEC;
            addPropagationDelay(delay);
        }

        if (state == STOP_STATE) {
            incrementFramesReceived();
            break;
        } else incrementErrors();
    }
    
    if (sendSVF(A_T, C_UA) < 0) return -1;
    incrementFramesSent();

    if (showStatistics) {
        double FER = -1, T_prop = -1, C = -1, received_bitrate = -1, efficiency = -1;
        double I_frame_size = (double) MAX_PAYLOAD_SIZE;

        if (statistics.framesSent > 0) FER = (double) statistics.errors / (double) statistics.framesSent;
        if (statistics.framesSent > 0) T_prop = statistics.propagationDelay / (double) statistics.framesSent;
        if (statistics.propagationDelay > 0) C = (double) statistics.framesSent / (double) (statistics.propagationDelay / MICROSECONDS_IN_SECOND); 
        if (statistics.propagationDelay > 0) received_bitrate = (double) (MAX_PAYLOAD_SIZE * 8) / (double) (statistics.propagationDelay / MICROSECONDS_IN_SECOND);

        if ((MAX_PAYLOAD_SIZE * 8 + 32) > 0) efficiency = (double) (MAX_PAYLOAD_SIZE * 8) / (double) (MAX_PAYLOAD_SIZE * 8 + 32);

        printf( "\nFinal Statistics\n"
                "   - Frames Sent: %d\n"
                "   - Frames Received: %d\n"
                "   - Errors: %d\n"
                "   - FER: %.2f\n"
                "   - Propagation Delay (T_prop): %.2f microseconds\n"
                "   - Link capacity (C): %.2f bits/second\n"
                "   - Size of I frame: %.2f bits\n"
                "   - Received bitrate: %.2f bits/second\n"
                "   - Efficiency: %.2f\n",
                statistics.framesSent,
                statistics.framesReceived,
                statistics.errors,
                FER, 
                T_prop, 
                C, 
                I_frame_size, 
                received_bitrate, 
                efficiency
        );
    }

    return closeSerialPort();
}

int sendSVF(unsigned char A, unsigned char C) {
    unsigned char buf_T[5] = {FLAG, A, C, A ^ C, FLAG};

    return (writeBytesSerialPort(buf_T, 5) < 0);
}

void nextTramaTx() {
    tramaTx = tramaTx == 0 ? 1 : 0;
}

void nextTramaRx() {
    tramaRx = tramaRx == 0 ? 1 : 0;
}

void alarmHandler(int signal) {
    printf("Alarm #%d\n", alarmCount + 1);

    alarmEnabled = FALSE;
    alarmCount++;
}

void incrementErrors() {
    statistics.errors++;
}

void incrementFramesSent() {
    statistics.framesSent++;
}

void incrementFramesReceived() {
    statistics.framesReceived++;
}

void addPropagationDelay(double delay) {
    statistics.propagationDelay += delay * MICROSECONDS_IN_SECOND;
}