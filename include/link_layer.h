// Link layer header.
// NOTE: This file must not be changed.

#ifndef _LINK_LAYER_H_
#define _LINK_LAYER_H_

#define FLAG 0x7E
#define ESC 0x7D
#define A_T 0x03
#define A_R 0x01
#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x0B
#define SUF_FLAG 0x5E
#define SUF_ESC 0x5D
#define REJ0 0x54
#define REJ1 0x55

#define C_N(Ns) (unsigned char)(Ns << 6)
#define C_RR(Nr) (0xAA | Nr)
#define C_REJ(Nr) (0x54 | Nr)

// SIZE of maximum acceptable payload.
// Maximum number of bytes that application layer should send to link layer
#define MAX_PAYLOAD_SIZE 1024

#define FALSE 0
#define TRUE 1

typedef enum
{
    LlTx,
    LlRx,
} LinkLayerRole;

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

typedef struct
{
    char serialPort[50];
    LinkLayerRole role;
    int baudRate;
    int nRetransmissions;
    int timeout;
} LinkLayer;

// Open a connection using the "port" parameters defined in struct linkLayer.
// Return "1" on success or "-1" on error.
int llopen(LinkLayer connectionParameters);

// Send data in buf with size bufSize.
// Return number of chars written, or "-1" on error.
int llwrite(const unsigned char *buf, int bufSize);

// Receive data in packet.
// Return number of chars read, or "-1" on error.
int llread(unsigned char *packet);

// Close previously opened connection.
// if showStatistics == TRUE, link layer should print statistics in the console on close.
// Return "1" on success or "-1" on error.
int llclose(int showStatistics);

int sendSVF(unsigned char A, unsigned char C);

void nextTramaTx();

void nextTramaRx();

#endif // _LINK_LAYER_H_
