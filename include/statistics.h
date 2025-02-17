#ifndef _STATISTICS_H_
#define _STATISTICS_H_

#include <stdio.h>
#include <sys/time.h>

#define TPROPAGATION    0  // propagation delay in ms
#define BCC1_ERROR      0   // percentage % of frames with BCC1 error
#define BCC2_ERROR      0   // percentage % of frames with BCC2 error

#define FILESIZE        10968

typedef struct {
    unsigned int bytesRead;
    unsigned int nFrames;
    unsigned int errorFrames;
    unsigned int retransmissions;
    struct timeval startTime;
    struct timeval endTime;
} Statistics;

double timeDiff(struct timeval start, struct timeval end);

double propagation_to_transmission_ratio(int baudrate, int maxPayload);

double received_bit_rate(Statistics stats);

double fer();

double optimal_efficiency(int baudrate, int maxPayload);

double actual_efficiency(Statistics stats, int baudrate);

#endif // _STATISTICS_H_
