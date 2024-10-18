#include "application_layer.h"
#include "link_layer.h"

#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <math.h>

#define C_START 1
#define C_DATA 2
#define C_END 3

void applicationLayerTransmitter(const char *filename);
void applicationLayerReceiver(const char *filename);
unsigned char *getControlPacket(unsigned int controlField, const char *filename, long int fileSize, unsigned int *controlPacketSize);
unsigned char *getDataPacket(unsigned int sequenceNumber, const unsigned char *data, unsigned int dataSize, unsigned int *dataPacketSize);

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename) {
    LinkLayer connectionParameters = {
        .baudRate = baudRate,
        .nRetransmissions = nTries,
        .timeout = timeout
    };

    strcpy(connectionParameters.serialPort, serialPort);
    connectionParameters.role = strcmp(role, "tx") == 0 ? LlTx : LlRx;

    if (llopen(connectionParameters) != 1) {
        printf("ERROR: Could not open connection\n");
        return;
    }

    if (connectionParameters.role == LlTx) {
        applicationLayerTransmitter(filename);
    } else {
        applicationLayerReceiver(filename);
    }
}

void applicationLayerTransmitter(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        printf("ERROR: Could not open file\n");
        return;
    }

    // current position of the file pointer
    long int initialFilePosition = ftell(file);

    // move the file pointer to the end of the file
    fseek(file, 0L, SEEK_END);
    
    long int fileSize = ftell(file) - initialFilePosition;

    // move the file pointer back to its original position
    fseek(file, initialFilePosition, SEEK_SET);

    unsigned int controlPacketSize;

    // Start Control Packet
    unsigned char *startControlPacket = getControlPacket(C_START, filename, fileSize, &controlPacketSize);

    if (llwrite(startControlPacket, controlPacketSize) == -1) {
        printf("Exit: error in start packet\n");
        free(startControlPacket);
        fclose(file);
        return;
    }
    
    // Data Packet
    unsigned char *fileData = (unsigned char*) malloc(fileSize);
    if (fileData == NULL) {
        printf("ERROR: Could not allocate memory for file data\n");
        free(startControlPacket);
        fclose(file);
        return;
    }
    fread(fileData, sizeof(unsigned char), fileSize, file);

    unsigned char sequenceNumber = 0;
    long int fileSizeRemaining = fileSize;
    while (fileSizeRemaining != 0) {
        unsigned int currentDataSize = fileSizeRemaining > MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : fileSizeRemaining;
        unsigned int dataPacketSize;
        unsigned char *dataPacket = getDataPacket(sequenceNumber, fileData + (fileSize - fileSizeRemaining), currentDataSize, &dataPacketSize);

        if (llwrite(dataPacket, dataPacketSize) == -1) {
            printf("Error in data packet\n");
            free(dataPacket);
            free(fileData);
            free(startControlPacket);
            fclose(file);
            return;
        }

        fileSizeRemaining -= currentDataSize;
        sequenceNumber = (sequenceNumber + 1) % 100;
        free(dataPacket);
    }

    // End Control Packet
    unsigned char *endControlPacket = getControlPacket(C_END, filename, fileSize, &controlPacketSize);

    if (llwrite(endControlPacket, controlPacketSize) == -1) {
        printf("Error in end packet\n");
        free(endControlPacket);
        free(fileData);
        free(startControlPacket);
        fclose(file);
        return;
    }

    fclose(file);
    free(startControlPacket);
    free(endControlPacket);
    free(fileData);
    llclose(TRUE);
}

void applicationLayerReceiver(const char *filename) {
    unsigned char *packet = (unsigned char*) malloc(MAX_PAYLOAD_SIZE);
    if (packet == NULL) {
        printf("ERROR: Could not allocate memory for packet\n");
        return;
    }

    // Start Control Packet
    int packetSize = 0;
    while ((packetSize = llread(packet)) < 0);

    // File Size
    unsigned long int receivedFileSize = 0;
    unsigned char fileSizeLength = packet[2];
    for (unsigned int i = 0; i < fileSizeLength; i++) {
        receivedFileSize |= (packet[2 + fileSizeLength - i] << (8 * i));
    } // important to verify if the entire file has been transferred correctly

    // File Name
    unsigned char fileNameLength = packet[4 + fileSizeLength];
    unsigned char *fileName = (unsigned char *) malloc(fileNameLength + 1);
    if (fileName == NULL) {
        printf("ERROR: Could not allocate memory for file name\n");
        free(packet);
        return;
    }

    memcpy(fileName, packet + fileSizeLength + 5, fileNameLength);
    fileName[fileNameLength] = '\0';
    
    // New File received
    FILE *newFile = fopen(filename, "wb+");
    /*FILE *newFile = fopen((char *)fileName, "wb+");
    if (newFile == NULL) {
        printf("ERROR: Could not open file for writing\n");
        free(packet);
        free(fileName);
        return;
    }*/

    unsigned long int totalReceivedDataSize = 0;
    while (TRUE) {
        while ((packetSize = llread(packet)) < 0);
        
        if (packetSize == 0) break;
        else if (packet[0] == C_DATA){
            unsigned char *dataBuffer = (unsigned char*) malloc(packetSize - 4);

            if (dataBuffer == NULL) {
                printf("ERROR: Could not allocate memory for data buffer\n");
                free(packet);
                free(fileName);
                fclose(newFile);
                return;
            }
            totalReceivedDataSize += (packetSize - 4);
            memcpy(dataBuffer, packet + 4, packetSize - 4);
            fwrite(dataBuffer, sizeof(unsigned char), packetSize - 4, newFile);
            free(dataBuffer);
        }
    }

    if (totalReceivedDataSize != receivedFileSize) {
        printf("ERROR: File size mismatch. Expected %lu bytes, but received %lu bytes.\n", receivedFileSize, totalReceivedDataSize);
    } else {
        printf("File received successfully. Total size: %lu bytes.\n", totalReceivedDataSize);
    }

    fclose(newFile);
    free(packet);
    free(fileName);
}

unsigned char *getControlPacket(unsigned int controlField, const char *filename, long int fileSize, unsigned int *controlPacketSize) {
    int fileSizeBytes = (int) ceil(log2f((float)fileSize) / 8.0);
    int filenameLength = strlen(filename);
    *controlPacketSize = 5 + fileSizeBytes + filenameLength;

    unsigned char *packet = (unsigned char*) malloc(*controlPacketSize);

    unsigned int pos = 0;

    packet[pos++] = controlField;

    // FILE SIZE
    // Set the file size field (Type: 0, Length: fileSizeBytes)
    packet[pos++] = 0;
    packet[pos++] = fileSizeBytes;

    // Split the file size into fileSizeBytes bytes and store in the packet
    for (int i = 0; i < fileSizeBytes; i++) {
        packet[pos + fileSizeBytes - 1 - i] = fileSize & 0xFF;
        fileSize >>= 8;
    }
    pos += fileSizeBytes;

    // FILE NAME
    // Set the filename field (Type: 1, Length: filenameLength)
    packet[pos++] = 1;
    packet[pos++] = filenameLength;
    memcpy(packet + pos, filename, filenameLength);

    return packet;
}

unsigned char *getDataPacket(unsigned int sequenceNumber, const unsigned char *data, unsigned int dataSize, unsigned int *dataPacketSize) {
    *dataPacketSize = 4 + dataSize;

    unsigned char *packet = (unsigned char*) malloc(*dataPacketSize);

    unsigned int pos = 0;

    packet[pos++] = C_DATA;
    packet[pos++] = sequenceNumber % 100;
    packet[pos++] = (dataSize >> 8) & 0xFF;
    packet[pos++] = dataSize & 0xFF;

    memcpy(packet + pos, data, dataSize);

    return packet;
}
