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

    llopen(connectionParameters);
    /*
    if (strcmp(role, "tx") == 0)
    {
        applicationLayerTransmitter(serialPort, role, baudRate, nTries, timeout, filename);
    }
    else if (strcmp(role, "rx") == 0)
    {
        printf("Receiver\n");
    }
    else
    {
        printf("Invalid role\n");
    }*/
}
/*
void applicationLayerTransmitter(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename) {

    LinkLayer connectionParameters = {
        .serialPort = serialPort,
        .role = LlTx,
        .baudRate = baudRate,
        .nRetransmissions = nTries,
        .timeout = timeout
    };
}

void applicationLayerReceiver(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename) {

    LinkLayer connectionParameters = {
        .serialPort = serialPort,
        .role = LlRx,
        .baudRate = baudRate,
        .nRetransmissions = nTries,
        .timeout = timeout
    };
}*/
