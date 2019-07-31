/**********************************************************
 * Author        : WangWei
 * Email         : 872408568@qq.com
 * Last modified : 2019-07-30 16:37:44
 * Filename      : httpclient.cpp
 * Description   : http客户端
 * *******************************************************/
#include  <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
using namespace std;

int main(int argc, char *argv[])
{
    int sockfd;
    int len;
    struct sockaddr_in address;
    int result;
    char ch = 'A';

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("0.0.0.0");
    address.sin_port = htons(9000);
    len = sizeof(address);
    result = connect(sockfd, (struct sockaddr*)&address, len);

    if(result == -1) {
        perror("connect error");
        exit(1);
    }

    write(sockfd, &ch, 1);
    read(sockfd, &ch, 1);
    cout << "char from server = " << ch << endl;
    close(sockfd);
    return 0;
}

