//
// Created by enzeyu on 2022/10/28.
//

#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>

// Input : hostname, port
// Output: file descriptor
int Socket(const char* hostname, int port){
    struct sockaddr_in sa;
    int ip;
    int socketFd;
    struct hostent *hs;

    // set IPv4, hostname and port
    memset(&sa,0,sizeof(sa));
    sa.sin_family = AF_INET;

    ip = inet_addr(hostname);

    if (ip != INADDR_NONE){ // copy ip to sin_addr if success
        memcpy(&sa.sin_addr,&ip,sizeof(ip));
    }else{
        hs = gethostbyname(hostname);
        if (hs==NULL){
            return -1;
        }
        memcpy(&sa.sin_addr, hs->h_addr, hs->h_length);
    }
    sa.sin_port = htons(port); // 16-bit host byte order to 16-bit network byte order

    // construct a connection
    socketFd = socket(AF_INET, SOCK_STREAM, 0); // construct stream socket and try to connect
    if (socketFd < 0){
        //fprintf(stderr,"construct stream error!\n");
        return socketFd;
    }
    if (connect(socketFd, (struct sockaddr *)&sa, sizeof(sa)) < 0){
        return -1;
    }
    return socketFd;
}