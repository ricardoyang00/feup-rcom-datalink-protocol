#include "statistics.h"

// Calculate the difference between two timeval structs in seconds.
double timeDiff(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
}

// a
double propagation_to_transmission_ratio(int baudrate, int maxPayload) {
    return (double) ((TPROPAGATION / 1000) / (maxPayload * 8 / baudrate));
}

double received_bit_rate(Statistics stats) {
    return (double) (stats.bytesRead * 8) / timeDiff(stats.startTime, stats.endTime);
}

// FER = P(bcc1 error) + P(bcc2 error) * (1 - P(bcc1 error))
// probability of any error in either BCC1 or BCC2
double fer() {
    return (double) (BCC1_ERROR) / 100 + (BCC2_ERROR / 100) * (1 - BCC1_ERROR / 100);
}

// Optimal Efficiency = (1 - FER) / (1 + 2a)
double optimal_efficiency(int baudrate, int maxPayload) {
    return (double) (1 - fer() / (1 + 2 * propagation_to_transmission_ratio(baudrate, maxPayload)));
}

// Actual Efficiency =  Actual Received Bitrate / Link Capacity
double actual_efficiency(Statistics stats, int baudrate) {
    //printf("Received Bit Rate: %f\n", received_bit_rate(stats));
    return (double) (received_bit_rate(stats) / baudrate);
}