// Data Link Protocol Macros

#define FLAG        0x7E
#define ESC         0x7D
#define A_T         0x03
#define A_R         0x01
#define C_SET       0x03
#define C_UA        0x07
#define C_DISC      0x0B
#define SUF_FLAG    0x5E
#define SUF_ESC     0x5D
#define C_INF(N)    ((N) ? 0x80 : 0x00)
#define C_RR(Nr)    (0xAA | Nr)
#define C_REJ(Nr)   (0x54 | Nr)
