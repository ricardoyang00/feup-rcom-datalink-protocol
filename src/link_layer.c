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

#define FLAG        0x7E
#define ESC         0x7D
#define A_T         0x03
#define A_R         0x01
#define C_SET       0x03
#define C_UA        0x07
#define C_DISC      0x0B
#define SUF_FLAG    0x5E
#define SUF_ESC     0x5D
#define C_INF0      0x00
#define C_INF1      0x80
#define C_RR(Nr)    (0xAA | Nr)
#define C_REJ(Nr)   (0x54 | Nr)

typedef enum {
    START_STATE, 
    FLAG_RCV,
    A_RCV, 
    C_RCV, 
    BCC_OK, 
    STOP_STATE,
    DATA_STATE,
    ESC_STATE
} LinkLayerState;

typedef struct {
    int errors;
    int framesSent;
    int framesReceived;
    double propagationDelay;
} LinkLayerStatistics;

void alarmHandler(int signal);
void alarmDisable();
void nextNs();
void nextNr();

int sendSVF(unsigned char A, unsigned char C);
int destuffing(unsigned char *buf, int bufSize, int *newSize, unsigned char *BCC2);
int receivePacket(unsigned char A_EXPECTED, unsigned char C_EXPECTED);
int receivePacketRetransmission(unsigned char A_EXPECTED, unsigned char C_EXPECTED, unsigned char A_SEND, unsigned char C_SEND);

int alarmEnabled = FALSE;
int alarmCount = 0;
int RETRANSMISSIONS = 0;
int TIMEOUT = 0;
LinkLayerRole ROLE;

unsigned char C_Ns = 0;
unsigned char C_Nr = 0;


////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    if (openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate) < 0) return -1;

    ROLE = connectionParameters.role;
    RETRANSMISSIONS = connectionParameters.nRetransmissions;
    TIMEOUT = connectionParameters.timeout;

    switch (ROLE) {
        case LlTx:
            if (receivePacketRetransmission(A_T, C_UA, A_T, C_SET) != 1) return -1;

            printf("TR: Connection Established!\n");

            break;

        case LlRx:
            if (receivePacket(A_T, C_SET) != 1) return -1;

            if (sendSVF(A_T, C_UA) != 1) return -1;

            printf("RCV: Connection Established!\n");

            break;
    }

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    if (buf == NULL) return -1;

    int tramaSize = bufSize + 6;
    unsigned char *trama = malloc(tramaSize);

    trama[0] = FLAG;
    trama[1] = A_T;
    trama[2] = C_Ns ? C_INF1 : C_INF0;
    trama[3] = trama[1] ^ trama[2];
    memcpy(trama + 4, buf, bufSize);

    unsigned char BCC2 = buf[0];
    for (int i = 1; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }


    // Stuffing
    int pos = 4;
    for (int i = 0; i < bufSize; i++) {
        switch (buf[i]) {
            case FLAG:
                trama = realloc(trama, ++tramaSize);
                trama[pos++] = ESC;
                trama[pos++] = SUF_FLAG;
                break;

            case ESC:
                trama = realloc(trama, ++tramaSize);
                trama[pos++] = ESC;
                trama[pos++] = SUF_ESC;
                break;

            default:
                trama[pos++] = buf[i];
                break;
        }
    }
    
    trama[pos++] = BCC2;
    trama[pos++] = FLAG;


    // Send trama
    LinkLayerState state = START_STATE;
    (void)signal(SIGALRM, alarmHandler);


    if (writeBytesSerialPort(trama, tramaSize) < 0) {
        free(trama);
        printf("TR: Error writing send command\n");
        return -1;
    }

    alarm(TIMEOUT);
    unsigned char byte_C = 0, byte_A = 0; 

    while (state != STOP_STATE && alarmCount <= RETRANSMISSIONS) {
        unsigned char byte;
        int result;
        if ((result = readByteSerialPort(&byte)) < 0) {
            free(trama);
            printf("TR: Error reading response\n");
            return -1;
        }

        else if (result > 0) {
            switch (state) {
                case START_STATE:
                    byte_C = 0;
                    byte_A = 0;
                    if (byte == FLAG) state = FLAG_RCV;
                    break;

                case FLAG_RCV:
                    if (byte == A_R || byte == A_T) {
                        state = A_RCV;
                        byte_A = byte;
                    }
                    else if (byte != FLAG) state = START_STATE;
                    break;

                case A_RCV:
                    if (byte == C_RR(0) || byte == C_RR(1) || byte == C_REJ(0) || byte == C_REJ(1)) {
                        state = C_RCV;
                        byte_C = byte;
                    }
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START_STATE;
                    break;

                case C_RCV:
                    if (byte == FLAG) state = FLAG_RCV;
                    else if ((byte_A ^ byte_C) == byte) state = BCC_OK;
                    else state = START_STATE;
                    break;

                case BCC_OK:
                    if (byte == FLAG) state = STOP_STATE;
                    else state = START_STATE;
                    break;

                default:
                    state = START_STATE;
            }
        }

        if (state == STOP_STATE) {
            if (byte_C == C_REJ(0) || byte_C == C_REJ(1)) {
                alarmEnabled = TRUE;
                alarmCount = 0;
                printf("TR: Received REJ Frame\n");
            }

            if (byte_C == C_RR(0) || byte_C == C_RR(1)) {
                printf("TR: Received RR Frame\n");
                alarmDisable();
                nextNs();
                free(trama);
                return bufSize;
            } 
            

        }

        if (alarmEnabled) {
            alarmEnabled = FALSE;

            if (alarmCount <= RETRANSMISSIONS) {
                if (writeBytesSerialPort(trama, tramaSize) < 0) {
                    printf("TR: Error writing send command\n");
                    return -1;
                }

                alarm(TIMEOUT);
            }

            state = START_STATE;
        }
    }

    alarmDisable();
    free(trama);

    return -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
    unsigned char byte_C = 0;
    int pos = 0;

    LinkLayerState state = START_STATE;

    while (state != STOP_STATE) {

        unsigned char byte = 0;
        int result;
        if ((result = readByteSerialPort(&byte)) < 0) {
            printf("RCV: Error reading response\n");
            return -1;
        }

        else if (result > 0) {
            switch (state) {
                case START_STATE:
                    pos = 0;
                    byte_C = 0;
                    if (byte == FLAG) state = FLAG_RCV;
                    break;

                case FLAG_RCV:
                    if (byte == A_T) state = A_RCV;
                    else if (byte != FLAG) state = START_STATE;
                    break;

                case A_RCV:
                    if (byte == C_INF0 || byte == C_INF1) {
                        state = C_RCV;
                        byte_C = byte;
                    }
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START_STATE;
                    break;

                case C_RCV:
                    if (byte == FLAG) state = FLAG_RCV;
                    else if ((A_T ^ byte_C) == byte) state = DATA_STATE;
                    else state = START_STATE;
                    break;

                case DATA_STATE:
                    if (byte == FLAG) {
                        int newSize = 0;
                        unsigned char BCC2 = 0;

                        if (destuffing(packet, pos, &newSize, &BCC2) != 1) {
                            printf("RCV: Error destuffing\n");
                            return -1;
                        }

                        unsigned char xor = packet[0];
                        for (int i = 1; i < newSize; i++) {
                            xor ^= packet[i];
                        }

                        // C_ and A_ are the fields of the receiver send frame
                        unsigned char C_;
                        if (xor == BCC2) {
                            if (byte_C == C_INF0) C_ = C_RR(1);
                            else C_ = C_RR(0);
                        } else {
                            if ((C_Nr == 0 && byte_C == C_INF1) ||
                                (C_Nr == 1 && byte_C == C_INF0)) {
                                if (byte_C == C_INF0) C_ = C_RR(1);
                                else C_ = C_RR(0);
                            } else {
                                if (byte_C == C_INF0) C_ = C_REJ(0);
                                else C_ = C_REJ(1);
                            }
                        }

                        if (sendSVF(A_R, C_) != 1) {
                            printf("RCV: Error sending response\n");
                            return -1;
                        }

                        if ((C_Nr == 0 && byte_C == C_INF0) || (C_Nr == 1 && byte_C == C_INF1)) {
                            nextNr();
                            return newSize;
                        }
                    } else {
                        packet[pos++] = byte;
                    }
                    break;

                default:
                    state = START_STATE;
            }
        }
    }

    return -1;
}

// Destuffing
// Returns 0 on success, -1 on error
// newSize is the size of the destuffed buffer
int destuffing(unsigned char *buf, int bufSize, int *newSize, unsigned char *BCC2) {
    if (buf == NULL || newSize == NULL) return -1;

    if (bufSize < 1) return 1;

    unsigned char *r = buf, *w = buf;

    while (r < buf + bufSize) {
        if (*r != ESC) *w++ = *r++;
        else {
            if (*(r + 1) == SUF_FLAG) *w++ = FLAG;
            else if (*(r + 1) == SUF_ESC) *w++ = ESC;
            r += 2;
        }
    }

    *BCC2 = *(w - 1);
    *newSize = w - buf - 1;
    return 1;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    switch (ROLE) {
        case LlTx:
            printf("Closing connection\n");
            if (receivePacketRetransmission(A_R, C_DISC, A_T, C_DISC) != -1) return closeSerialPort();
            
            printf("Sending SVF\n");
            if (sendSVF(A_R, C_UA) != -1) return closeSerialPort();
            break;
            
        case LlRx:
            if (receivePacket(A_T, C_DISC) != -1) return closeSerialPort();

            if (receivePacketRetransmission(A_R, C_UA, A_R, C_DISC) != -1) return closeSerialPort();
            break;

        default:
            return -1;

    }

    printf("Connection closed\n");

    return closeSerialPort();
}

void alarmHandler(int signal) {
    printf("Alarm #%d\n", alarmCount + 1);
    alarmCount++;
    alarmEnabled = TRUE;
}

void alarmDisable() {
    alarm(0);
    alarmEnabled = FALSE;
    alarmCount = 0;
}

// Send Supervision Frame
// Returns 1 on success, -1 on error
int sendSVF(unsigned char A, unsigned char C) {
    unsigned char buf_T[5] = {FLAG, A, C, A ^ C, FLAG};

    return (writeBytesSerialPort(buf_T, 5) < 0) ? -1 : 1;
}

void nextNs() {
    C_Ns = C_Ns ? 0 : 1;
}

void nextNr() {
    C_Nr = C_Nr ? 0 : 1;
}

int receivePacketRetransmission(unsigned char A_EXPECTED, unsigned char C_EXPECTED, unsigned char A_SEND, unsigned char C_SEND) {
    LinkLayerState state = START_STATE;

    (void)signal(SIGALRM, alarmHandler);

    if (sendSVF(A_SEND, C_SEND) != 1) return -1;

    alarm(TIMEOUT); 

    while (state != STOP_STATE && alarmCount <= RETRANSMISSIONS) {
        unsigned char byte = 0;
        int result;
        if((result = readByteSerialPort(&byte)) < 0) {
            printf("Error reading UA frame\n");
            return -1;
        }

        else if(result > 0){
            switch (state) {
                case START_STATE:
                    if (byte == FLAG) state = FLAG_RCV;
                    break;

                case FLAG_RCV:
                    if (byte == A_EXPECTED) state = A_RCV;
                    else if (byte != FLAG) state = START_STATE;
                    break;

                case A_RCV:
                    if (byte == C_EXPECTED) state = C_RCV;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START_STATE;
                    break;

                case C_RCV:
                    if (byte == FLAG) state = FLAG_RCV;
                    else if ((C_EXPECTED ^ A_EXPECTED) == byte) state = BCC_OK;
                    else state = START_STATE;
                    break;

                case BCC_OK:
                    if (byte == FLAG) state = STOP_STATE;
                    else state = START_STATE;
                    break;

                default:
                    state = START_STATE;

            }
        }

        if (state == STOP_STATE) {
            alarmDisable();
            return 1;
        }
        
        else if (alarmEnabled) {
            alarmEnabled = FALSE;
            if (alarmCount <= RETRANSMISSIONS) {
                if (sendSVF(A_SEND, C_SEND) != 1) {
                    printf("Error writing send command\n");
                    return -1;
                }
                alarm(TIMEOUT);
            }
            
            state = START_STATE;
        }
    }

    alarmDisable();
    printf("returning -1\n");
    
    return -1;
}

int receivePacket(unsigned char A_EXPECTED, unsigned char C_EXPECTED) {
    LinkLayerState state = START_STATE;

    while (state != STOP_STATE) {
        unsigned char byte = 0;
        int result;
        if((result = readByteSerialPort(&byte)) < 0) {
            printf("Error reading response\n");
            return -1;
        }

        else if(result > 0){
            switch (state) {
                case START_STATE:
                    if (byte == FLAG) state = FLAG_RCV;
                    break;

                case FLAG_RCV:
                    if (byte == A_EXPECTED) state = A_RCV;
                    else if (byte != FLAG) state = START_STATE;
                    break;

                case A_RCV:
                    if (byte == C_EXPECTED) state = C_RCV;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START_STATE;
                    break;

                case C_RCV:
                    if (byte == FLAG) state = FLAG_RCV;
                    else if ((A_EXPECTED ^ C_EXPECTED) == byte) state = BCC_OK;
                    else state = START_STATE;
                    break;

                case BCC_OK:
                    if (byte == FLAG) state = STOP_STATE;
                    else state = START_STATE;
                    break;

                default:
                    state = START_STATE;

            }
        }
    }

    return 1;
}