// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include "protocol.h"

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
int sendPacketPacket(unsigned char A, unsigned char C);
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

            if (sendPacketPacket(A_T, C_UA) != 1) return -1;

            printf("RCV: Connection Established!\n");

            break;
    }

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) 
{
    if (buf == NULL) return -1;

    int tramaSize = bufSize + 6;
    unsigned char *trama = malloc(tramaSize);

    // Create trama header
    trama[0] = FLAG;
    trama[1] = A_T;
    trama[2] = C_Ns ? C_INF(1) : C_INF(0);
    trama[3] = trama[1] ^ trama[2];
    memcpy(trama + 4, buf, bufSize);

    unsigned char BCC2 = buf[0];
    for (int i = 1; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }

    // Data and stuffing
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
    
    // tail
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

    while (state != STOP_STATE && alarmCount <= RETRANSMISSIONS) 
    {
        int result;
        unsigned char byte;
        
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
                printf("TR: Frame rejected, resending frame\n");
            }

            else if (byte_C == C_RR(0) || byte_C == C_RR(1)) {
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
int llread(unsigned char *packet) 
{
    unsigned char byte_C = 0;
    int pos = 0;

    LinkLayerState state = START_STATE;

    while (state != STOP_STATE) 
    {
        int result;
        unsigned char byte = 0;
        
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
                    if (byte == C_INF(0) || byte == C_INF(1)) {
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

                        // create BCC2
                        unsigned char xor = packet[0];
                        for (int i = 1; i < newSize; i++) {
                            xor ^= packet[i];
                        }

                        unsigned char C_;   // the response frame

                        if (xor == BCC2) {
                            // BCC2 correct, send a positive acknowledgment (RR)
                            C_ = (byte_C == C_INF(0)) ? C_RR(1) : C_RR(0);
                        } 
                        
                        else {
                            // BCC2 incorrect
                            if ((C_Nr == 0 && byte_C == C_INF(1)) || (C_Nr == 1 && byte_C == C_INF(0))) {
                                // received frame is the expected, send a positive acknowledgment (RR)
                                C_ = (byte_C == C_INF(0)) ? C_RR(1) : C_RR(0);
                            } else {
                                // received frame is not the expected, send a negative acknowledgment (REJ)
                                C_ = (byte_C == C_INF(0)) ? C_REJ(0) : C_REJ(1);
                            }
                        }

                        state = START_STATE;

                        if (sendPacketPacket(A_R, C_) != 1) {
                            printf("RCV: Error sending response\n");
                            return -1;
                        }

                        if (C_ == C_REJ(0) || C_ == C_REJ(1)) {
                            break;
                        }

                        // update sequence number
                        if ((C_Nr == 0 && byte_C == C_INF(0)) || (C_Nr == 1 && byte_C == C_INF(1))) {
                            nextNr();
                            return newSize;
                        }

                        printf("RCV: Discarding frame, duplicate\n");
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

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    switch (ROLE) {

        case LlTx:
            if (receivePacketRetransmission(A_R, C_DISC, A_T, C_DISC) == -1) return -1;
            
            if (sendPacketPacket(A_R, C_UA) != -1) return closeSerialPort();

            break;
            
        case LlRx:
            if (receivePacket(A_T, C_DISC) == 1) {
                //if (receivePacketRetransmission(A_T, C_UA, A_R, C_DISC) != 1) return -1; // something goes wrong
                if (sendPacketPacket(A_R, C_DISC) != 1) return -1;
            }
            
            break;

        default:
            return -1;

    }

    printf("Connection closed\n");

    return closeSerialPort();
}


////////////////////////////////////////////////
// AUXILIARY FUNCTIONS
////////////////////////////////////////////////

// Alarm handler
void alarmHandler(int signal) 
{
    printf("Alarm #%d\n", alarmCount + 1);
    alarmCount++;
    alarmEnabled = TRUE;
}

// Disable alarm
void alarmDisable() 
{
    alarm(0);
    alarmEnabled = FALSE;
    alarmCount = 0;
}

// Switch Ns between 0 and 1
void nextNs() 
{
    C_Ns = C_Ns ? 0 : 1;
}

// Switch Nr between 0 and 1
void nextNr() 
{
    C_Nr = C_Nr ? 0 : 1;
}

// Send Supervision Frame and Unnumbered Frame
// Returns 1 on success, -1 on error
int sendPacketPacket(unsigned char A, unsigned char C) 
{
    unsigned char buf_T[5] = {FLAG, A, C, A ^ C, FLAG};

    return (writeBytesSerialPort(buf_T, 5) < 0) ? -1 : 1;
}

/**
 * @brief Perform byte destuffing on the input buffer.
 *
 * This function processes the input buffer to remove escape sequences and
 * reconstruct the original data. It also extracts the BCC2 (Block Check Character 2)
 * from the buffer.
 *
 * @param buf The input buffer containing the stuffed data.
 * @param bufSize The size of the input buffer.
 * @param newSize Pointer to an integer where the new size of the buffer will be stored.
 * @param BCC2 Pointer to an unsigned char where the BCC2 will be stored.
 * @return int Returns 1 on success, -1 on error (e.g., null pointers), 1 on invalid buffer size.
 */
int destuffing(unsigned char *buf, int bufSize, int *newSize, unsigned char *BCC2) 
{
    if (buf == NULL || newSize == NULL) return -1;
    if (bufSize < 1) return 1;

    unsigned char *r = buf, *w = buf;

    while (r < buf + bufSize) {
        if (*r != ESC) *w++ = *r++; // if not escape, copy byte
        else {
            // if ESC, check next and replace with FLAG/ESC
            if (*(r + 1) == SUF_FLAG) *w++ = FLAG;  
            else if (*(r + 1) == SUF_ESC) *w++ = ESC;
            r += 2;
        }
    }

    *BCC2 = *(w - 1);
    *newSize = w - buf - 1;

    return 1;
}

int receivePacket(unsigned char A_EXPECTED, unsigned char C_EXPECTED) 
{
    LinkLayerState state = START_STATE;

    while (state != STOP_STATE) 
    {
        int result;
        unsigned char byte = 0;
        
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

// Receive packet with retransmission
// Returns 1 on success, -1 on error
int receivePacketRetransmission(unsigned char A_EXPECTED, unsigned char C_EXPECTED, unsigned char A_SEND, unsigned char C_SEND) 
{
    LinkLayerState state = START_STATE;

    (void)signal(SIGALRM, alarmHandler);

    if (sendPacketPacket(A_SEND, C_SEND) != 1) return -1;

    alarm(TIMEOUT); 

    while (state != STOP_STATE && alarmCount <= RETRANSMISSIONS) 
    {
        int result;
        unsigned char byte = 0;
        
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

                if (sendPacketPacket(A_SEND, C_SEND) != 1) {
                    printf("Error writing send command\n");
                    return -1;
                }

                alarm(TIMEOUT);
            }
            
            state = START_STATE;
        }
    }

    alarmDisable();
    
    return -1;
}



