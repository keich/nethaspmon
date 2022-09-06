/*

MIT License

Copyright (c) 2022 Alexander Zazhigin mykeich@yandex.ru

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include "nethaspmon.h"


#define DEFAULT_BUFLEN 30000
#define DEFAULT_PORT "27015"

DWORD WINAPI http_run(LPVOID lpParam) {
    WSADATA wsaData;
    int iResult;

    SOCKET ListenSocket = INVALID_SOCKET;
    SOCKET ClientSocket = INVALID_SOCKET;

    struct addrinfo *result = NULL;
    struct addrinfo hints;

    int iSendResult;
    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Resolve the server address and port
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if (iResult != 0) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Create a SOCKET for the server to listen for client connections.
    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        printf("socket failed with error: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Setup the TCP listening socket
    iResult = bind(ListenSocket, result->ai_addr, (int) result->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        printf("bind failed with error: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

    iResult = listen(ListenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) {
        printf("listen failed with error: %d\n", WSAGetLastError());
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }
    int data_size = 0;
    int maxsize = MAX_BUFF_SIZE + 1024;
    char data[maxsize];
    while (1) {
        printf("Wait clients on port %s\n", DEFAULT_PORT);
        ClientSocket = accept(ListenSocket, NULL, NULL);
        if (ClientSocket == INVALID_SOCKET) {
            printf("accept failed with error: %d\n", WSAGetLastError());
            closesocket(ListenSocket);
            WSACleanup();
            return 1;
        }

        iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
        if (iResult > 0) {
            printf("Bytes received: %d\n", iResult);
            printf("%.*s\n", iResult, recvbuf);
            recvbuf[iResult] = '\0';
            //SvcReportEventInfo(TEXT(recvbuf));
            int disco = 0;
            if (!strncmp("GET /disco", recvbuf, 10)) {
                disco = 1;
            }
            printf("Disco = %d\n", disco);

            WaitForSingleObject(ghMutex, INFINITE);
            data_size = snprintf(data, maxsize, "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n {}\n");
            if (!disco) {
                if (StrBufferLen(gdataResult) > 0) {
                    data_size = snprintf(data, maxsize, "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n %s\n", gdataResult->data);
                }
            }else{
                if (StrBufferLen(gdataDiscoResult)) {
                    data_size = snprintf(data, maxsize, "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n %s\n", gdataDiscoResult->data);
                }
            }

            ReleaseMutex(ghMutex);
            //SvcReportEventInfo(TEXT(gdataResult.data));
            iSendResult = send(ClientSocket, data, data_size, 0);
            if (iSendResult == SOCKET_ERROR) {
                printf("send failed with error: %d\n", WSAGetLastError());
            }
            printf("Bytes sent: %d\n", iSendResult);
        } else if (iResult == 0)
            printf("Connection closing...\n");
        else {
            printf("recv failed with error: %d\n", WSAGetLastError());
        }

        iResult = shutdown(ClientSocket, SD_RECEIVE);
        if (iResult == SOCKET_ERROR) {
            printf("shutdown failed with error: %d\n", WSAGetLastError());
        }
        printf("Connection closing...\n");
        closesocket(ClientSocket);
    }

    closesocket(ListenSocket);
    WSACleanup();
    return 0;
}

int http_start() {
    DWORD dwThreadId;
    HANDLE hThread = CreateThread(
    NULL,                   // default security attributes
            0,                      // use default stack size
            http_run,       // thread function name
            NULL,          // argument to thread function
            0,                      // use default creation flags
            &dwThreadId);   // returns the thread identifier
    if (hThread == NULL) {
        printf("Can not create thread\n");
        return 1;
    }
    return 0;
}
