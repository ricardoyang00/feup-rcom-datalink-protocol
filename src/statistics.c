#include "statistics.h"

// Calculate the difference between two timeval structs in seconds.
double timeDiff(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
}

// a
double propagation_to_transmission_ratio(int baudrate, int maxPayload) {
    return ((double) TPROPAGATION / 1000.0) / ((double) maxPayload * 8.0 / (double) baudrate);
}

double received_bit_rate(Statistics stats) {
    return (double) (FILESIZE * 8) / timeDiff(stats.startTime, stats.endTime);
}

// FER = P(bcc1 error) + P(bcc2 error) * (1 - P(bcc1 error))
// probability of any error in either BCC1 or BCC2
double fer() {
    double bcc1_error_rate = (double) BCC1_ERROR / 100.0;
    double bcc2_error_rate = (double) BCC2_ERROR / 100.0;
    return bcc1_error_rate + bcc2_error_rate * (1 - bcc1_error_rate);
}

// Optimal Efficiency = (1 - FER) / (1 + 2a)
double optimal_efficiency(int baudrate, int maxPayload) {
    double fer_value = fer();
    double a = propagation_to_transmission_ratio(baudrate, maxPayload);
    return (1 - fer_value) / (1 + 2 * a);
}

// Actual Efficiency =  Actual Received Bitrate / Link Capacity
double actual_efficiency(Statistics stats, int baudrate) {
    return (double) (received_bit_rate(stats) / baudrate);
}