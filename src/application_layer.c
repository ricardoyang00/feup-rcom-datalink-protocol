// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer linkLayer = {
        .baudRate = baudRate,
        .nRetransmissions = nTries,
        .timeout = timeout
    };

    strcpy(linkLayer.serialPort,serialPort);
    linkLayer.role = strcmp(role,"tx") == 0 ? LlTx : LlRx;

    if (llopen(linkLayer) != 1) return;

}

