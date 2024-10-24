// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include "protocol.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define MAX_FILENAME 100

int sequenceNumber = 0;
size_t totalBytesRead = 0;

int sendPacketData(size_t nBytes, unsigned char *data) 
{
    if(data == NULL) return -1;
    
    unsigned char *packet = (unsigned char *) malloc(nBytes + 4);
    if(packet == NULL) return -1;
    
    packet[0] = C_DATA;
    packet[1] = (sequenceNumber++) % 100;
    packet[2] = nBytes >> 8;
    packet[3] = nBytes & 0xFF;

    memcpy(packet + 4, data, nBytes);

    int result = llwrite(packet, nBytes + 4);

    free(packet);
    return result;
}

// Function to convert a size_t value to an array of unsigned char (octets)
/**
 * @brief Converts a size_t value to an array of unsigned char (octets).
 *
 * This function converts a size_t value to an array of unsigned char (octets) and
 * returns the array. The length of the array is stored in the variable pointed to by size.
 *
 * @param value The size_t value to be converted.
 * @param size Pointer to an unsigned char where the length of the array will be stored.
 * @return unsigned char* Pointer to the array of octets, or NULL if memory allocation fails.
 */
unsigned char * sizetouchar(size_t value, unsigned char *size)
{
    if (size == NULL) return NULL; 
    
    size_t temp = value, l = 0;

    do {
        l++;
        temp >>= 8;
    } while (temp);

    unsigned char *bytes = malloc(l);
    if (bytes == NULL) return NULL;

    for (size_t i = 0; i < l; i++) {
        bytes[i] = value & 0xFF;
        value >>= 8;
    }

    *size = l;
    return bytes;
}

// Function to convert an array of unsigned char (octets) to a size_t value
/**
 * @brief Converts an array of unsigned char (octets) to a size_t value.
 *
 * This function converts an array of unsigned char (octets) to a size_t value.
 *
 * @param n The number of octets in the array.
 * @param numbers Pointer to the array of unsigned char (octets).
 * @return size_t The converted size_t value.
 */
size_t uchartosize (unsigned char n, unsigned char * numbers)
{
    if(numbers == NULL) return 0;

    size_t value = 0;
    size_t power = 1;

    for(int i = 0; i < n; i++) {
        value += numbers[i] * power;
        power <<= 8;
    }

    return value;
}

int sendPacketControl(unsigned char C, const char *filename, size_t file_size)
{
    if(filename == NULL) return -1;
    
    unsigned char L1 = 0;
    unsigned char * V1 = sizetouchar(file_size, &L1);
    if(V1 == NULL) return -1;

    unsigned char L2 = (unsigned char) strlen(filename);

    unsigned char *packet = (unsigned char *) malloc(5 + L1 + L2);
    if(packet == NULL) {
        free(V1);
        return -1;
    }

    size_t pos = 0;
    packet[pos++] = C;

    // file size (V1)
    packet[pos++] = T_FILESIZE;
    packet[pos++] = L1;
    memcpy(packet + pos, V1, L1); 
    pos += L1;
    free(V1);

    // file name (V2)
    packet[pos++] = T_FILENAME;
    packet[pos++] = L2;
    memcpy(packet + pos, filename, L2); 
    pos += L2;  

    int result = llwrite(packet, (int) pos);

    free(packet);
    return result;
}

int readPacketData(unsigned char *buff, size_t *newSize, unsigned char *dataPacket)
{
    if (buff == NULL) return -1;
    if (buff[0] != C_DATA) return -1;

    *newSize = buff[2] * 256 + buff[3];
    memcpy(dataPacket, buff + 4, *newSize);

    return 1;
}

int readPacketControl(unsigned char *buff, int *isEnd)
{   
    if (buff == NULL) return -1;

    size_t pos = 0;

    if(buff[pos] == C_END) *isEnd = TRUE;
    else if (buff[pos] != C_START) return -1;

    // file size (V1)
    pos++;
    if (buff[pos++] != T_FILESIZE) return -1;
    unsigned char L1 = buff[pos++]; // V1 field size

    unsigned char * V1 = malloc(L1);
    if(V1 == NULL) return -1;

    memcpy(V1, buff + pos, L1);
    pos += L1;

    size_t file_size = uchartosize(L1, V1);
    free(V1);

    // name (V2)
    if(buff[pos++] != T_FILENAME) return -1;
    unsigned char L2 = buff[pos++]; // V2 field size

    char * file_name = malloc(MAX_FILENAME);
    if(file_name == NULL) return -1;

    memcpy(file_name, buff + pos, L2);
    file_name[L2] = '\0';


    if(buff[0] == C_START){
        printf("[INFO] Started receiving file: '%s'\n", file_name);
    } else if(buff[0] == C_END){
        if (file_size != totalBytesRead) {
            printf("[Warning] The received file size doesn't match the original file\n");
        }

        printf("[INFO] Finished receiving file: '%s'\n", file_name);
    }
    
    free(file_name);
    return 1;
}

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    if(serialPort == NULL || role == NULL || filename == NULL){
        printf("[ERROR] Initialization error: One or more required arguments are NULL.");
        return;
    }

    if (strlen(filename) > MAX_FILENAME) {
        printf("[ALERT] The lenght of the given file name is greater than what is supported: %d characters'\n", MAX_FILENAME);
        return;
    }
        
    LinkLayer connectionParametersApp;
    strncpy(connectionParametersApp.serialPort, serialPort, sizeof(connectionParametersApp.serialPort)-1);
    connectionParametersApp.role = strcmp(role, "tx") ? LlRx : LlTx; 
    connectionParametersApp.baudRate = baudRate;
    connectionParametersApp.nRetransmissions = nTries;
    connectionParametersApp.timeout = timeout;

    if (llopen(connectionParametersApp) == -1) {
        printf("[ERROR] Link layer error: Failed to open the connection.");
        //llclose(FALSE);
        return;
    }
    
    if (connectionParametersApp.role == LlTx) {
        size_t bytesRead = 0;
        unsigned char *buffer = (unsigned char *) malloc(MAX_PAYLOAD_SIZE + 20);
        if(buffer == NULL) {
            printf(" [ERROR] Memory allocation error at buffer creation.");
            llclose(FALSE);
            return;
        }

        FILE* file = fopen(filename, "rb");
        if(file == NULL) {
            printf("[ERROR] File error: Unable to open the file for reading.");
            fclose(file);
            free(buffer);
            llclose(FALSE);
            return;
        }

        fseek(file, 0, SEEK_END);
        size_t file_size = ftell(file);
        rewind(file);

        if(sendPacketControl(C_START, filename, file_size) == -1) {
            printf("[ERROR] Transmission error: Failed to send the START packet control.");
            fclose(file);
            llclose(FALSE);
            return;
        }

        while ((bytesRead = fread(buffer, 1, MAX_PAYLOAD_SIZE, file)) > 0) {
            
            if(sendPacketData(bytesRead, buffer) == -1){
                printf("[ERROR] Transmission error: Failed to send the DATA packet control.");
                fclose(file);
                llclose(FALSE);
                return;
            }
        }

        if(sendPacketControl(C_END, filename, file_size) == -1){
            printf("[ERROR] Transmission error: Failed to send the END packet control.");
            fclose(file);
            llclose(FALSE);
            return;
        }

        fclose(file);
    } 
    
    if (connectionParametersApp.role == LlRx) {
        
        unsigned char * buf = malloc(MAX_PAYLOAD_SIZE + 20);
        unsigned char * packet = malloc(MAX_PAYLOAD_SIZE + 20);
        if(buf == NULL || packet == NULL){
            printf("[ERROR] Initialization error: One or more buffers pointers are NULL.");
            llclose(FALSE);
            return;
        }
   
        FILE *file = fopen(filename, "wb");
        
        if(file == NULL) {
            printf("[ERROR] File error: Unable to open the file for writing.");
            fclose(file);
            llclose(FALSE);
            return;
        }

        size_t bytes_readed = 0;

        int isEnd = FALSE;

        while(!isEnd){

            if((bytes_readed = llread(buf)) == -1) {
                printf("[ERROR] Link layer error: Failed to read from the link.");
                fclose(file);
                llclose(FALSE);
                return;
            }
            
            if(buf[0] == C_START || buf[0] == C_END){

                if(readPacketControl(buf, &isEnd) == -1) {
                    printf("[ERROR] Packet error: Failed to read control packet.");
                    fclose(file);
                    llclose(FALSE);
                    return;
                }

            } else if(buf[0] == C_DATA){
                
                if(readPacketData(buf, &bytes_readed, packet) == -1) {
                    printf("[ERROR] Packet error: Failed to read data packet.");
                    fclose(file);
                    llclose(FALSE);
                    return;
                }
                fwrite(packet, 1, bytes_readed, file);
                totalBytesRead += bytes_readed;
            }
        }

        fclose(file);
    }


    if (llclose(TRUE) == -1) {
        printf("[ERROR] Link layer error: Failed to close the connection.");
        return;
    }

    printf("[SUCCESS] Connection closed successfully.\n");
}