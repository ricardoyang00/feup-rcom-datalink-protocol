// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer connectionParameters = {
        .baudRate = baudRate,
        .nRetransmissions = nTries,
        .timeout = timeout
    };

    strcpy(connectionParameters.serialPort,serialPort);
    connectionParameters.role = strcmp(role,"tx") == 0 ? LlTx : LlRx;

    if (llopen(connectionParameters) != 1) return;

    if (connectionParameters.role == LlTx) applicationLayerTransmitter(connectionParameters);
    else if (connectionParameters.role == LlRx) applicationLayerReceiver(connectionParameters);
    else {
        printf("ERROR: Invalid role\n");
        return;
    }
}

void applicationLayerTransmitter(LinkLayer connectionParameters) {

}

void applicationLayerReceiver(LinkLayer connectionParameters) {

}
