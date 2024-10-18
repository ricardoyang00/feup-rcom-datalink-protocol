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

unsigned char *getControlPacket(unsigned int controlField, const char *filename, long int fileSize, unsigned int *controlPacketSize);
unsigned char *getDataPacket(unsigned int sequenceNumber, const unsigned char *data, unsigned int dataSize, unsigned int *dataPacketSize);
void parseDataPacket(const unsigned char *packet, int packetSize, unsigned char *buffer);

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
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
        // Transmitter role

        printf("TR: Started Transmitting\n");

        // Open the file specified in filename in binary read mode
        FILE *file = fopen(filename, "rb");
        if (file == NULL) {
            printf("ERROR: Could not open file\n");
            return;
        }

        // Save the current position of the file pointer
        long initialFilePosition = ftell(file);

        // Move the file pointer to the end of the file
        fseek(file, 0L, SEEK_END);

        // Calculate the size of the file in bytes
        long fileSize = ftell(file) - initialFilePosition;

        // Move the file pointer back to its original position
        fseek(file, initialFilePosition, SEEK_SET);

        // Define a variable to store the size of the control packet
        unsigned int controlPacketSize;

        // Generate the control packet for the start of the transmission
        unsigned char *startControlPacket = getControlPacket(1, filename, fileSize, &controlPacketSize);

        // Send the start control packet
        printf("TR: Sending start control packet\n");
        if (llwrite(startControlPacket, controlPacketSize) == -1) {
            printf("Exit: error in start packet\n");
            free(startControlPacket);
            fclose(file);

            return;
        }
        printf("TR: Sent start control packet\n");
        
        // Allocate memory to read the entire file into a buffer
        unsigned char *fileData = malloc(fileSize);
        if (fileData == NULL) {
            printf("ERROR: Could not allocate memory for file data\n");
            free(startControlPacket);
            fclose(file);
            return;
        }
        fread(fileData, sizeof(unsigned char), fileSize, file);

        // Initialize the sequence number
        unsigned char sequenceNumber = 0;

        // Transmit the file in chunks
        long fileSizeRemaining = fileSize;
        while (fileSizeRemaining > 0) {
            // Determine the size of the current data chunk
            unsigned int currentDataSize = fileSizeRemaining > MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : fileSizeRemaining;

            // Generate the data packet
            unsigned int dataPacketSize;
            unsigned char *dataPacket = getDataPacket(sequenceNumber, fileData + (fileSize - fileSizeRemaining), currentDataSize, &dataPacketSize);

            // Send the data packet
            printf("TR: Sending data packet #%d\n", sequenceNumber);
            if (llwrite(dataPacket, dataPacketSize) == -1) {
                printf("Error in data packet\n");
                free(dataPacket);
                free(fileData);
                free(startControlPacket);
                fclose(file);
                return;
            }

            // Update the remaining file size and sequence number
            fileSizeRemaining -= currentDataSize;
            sequenceNumber = (sequenceNumber + 1) % 100;

            // Free the allocated memory for the data packet
            free(dataPacket);
        }

        // Generate the control packet for the end of the transmission
        unsigned char *endControlPacket = getControlPacket(3, filename, fileSize, &controlPacketSize);

        // Send the end control packet
        printf("TR: Finished Transmitting\n");
        if (llwrite(endControlPacket, controlPacketSize) == -1) {
            printf("Error in end packet\n");
            free(endControlPacket);
            free(fileData);
            free(startControlPacket);
            fclose(file);
            return;
        }

        // Close the file and the connection
        fclose(file);

        // Free the allocated memory for the control packets and file data
        free(startControlPacket);
        free(endControlPacket);
        free(fileData);
    } else {
        // Receiver role

        printf("RCV: Started Receiving\n");

        // Allocate memory for the packet
        unsigned char *packet = malloc(MAX_PAYLOAD_SIZE);
        if (packet == NULL) {
            printf("ERROR: Could not allocate memory for packet\n");
            return;
        }

        // Read the start control packet
        int packetSize = -1;
        while ((packetSize = llread(packet)) < 0);
        printf("RCV: Received start control packet\n");

        // Extract the file size from the start control packet
        unsigned long int receivedFileSize = 0;
        unsigned char fileSizeLength = packet[2];
        for (unsigned int i = 0; i < fileSizeLength; i++) {
            receivedFileSize |= (packet[3 + fileSizeLength - 1 - i] << (8 * i));
        }

        // Extract the file name from the start control packet
        unsigned char fileNameLength = packet[3 + fileSizeLength + 1];
        unsigned char *fileName = (unsigned char *)malloc(fileNameLength + 1);
        if (fileName == NULL) {
            printf("ERROR: Could not allocate memory for file name\n");
            free(packet);
            return;
        }

        memcpy(fileName, packet + 3 + fileSizeLength + 2, fileNameLength);
        fileName[fileNameLength] = '\0';

        // Open the new file for writing
        FILE *newFile = fopen((char *)fileName, "wb+");
        if (newFile == NULL) {
            printf("ERROR: Could not open file for writing\n");
            free(packet);
            free(fileName);
            return;
        }

        // Read and write data packets
        printf("RCV: Started Receiving data packets...\n");
        while (TRUE) {
            while ((packetSize = llread(packet)) < 0);
            //printf("RCV: Received data packet #%d\n", packet[1]);
            
            if (packetSize == 0) {
                printf("RCV: End of transmission\n");
                break;
            }
            if (packet[0] != 3) {
                unsigned char *dataBuffer = malloc(packetSize - 4);
                if (dataBuffer == NULL) {
                    printf("ERROR: Could not allocate memory for data buffer\n");
                    free(packet);
                    free(fileName);
                    fclose(newFile);
                    return;
                }
                parseDataPacket(packet, packetSize, dataBuffer);
                fwrite(dataBuffer, sizeof(unsigned char), packetSize - 4, newFile);
                free(dataBuffer);
            }
        }

        // Close the file and free allocated memory
        fclose(newFile);
        free(packet);
        free(fileName);
    }

    llclose(TRUE);
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
    // Calculate the total size of the data packet
    *dataPacketSize = 1 + 1 + 2 + dataSize;

    // Allocate memory for the data packet
    unsigned char *packet = (unsigned char*) malloc(*dataPacketSize);

    // Initialize the position index for the packet
    unsigned int pos = 0;

    // Set the control field (value: 2 for data)
    packet[pos++] = 2;

    // Set the sequence number
    packet[pos++] = sequenceNumber % 100; // Ensure the sequence number is between 0 and 99

    // Set the size of the data field (L2 L1)
    packet[pos++] = (dataSize >> 8) & 0xFF; // L2
    packet[pos++] = dataSize & 0xFF; // L1

    // Copy the data into the packet
    memcpy(packet + pos, data, dataSize);

    return packet;
}

void parseDataPacket(const unsigned char *packet, int packetSize, unsigned char *buffer) {
    memcpy(buffer, packet + 4, packetSize - 4);
}