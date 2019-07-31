/**********************************************************
 * Author        : WangWei
 * Email         : 872408568@qq.com
 * Last modified : 2019-07-27 21:26:48
 * Filename      : http.c
 * Description   : 超轻量型服务器(Tinyhttpd)
 * *******************************************************/
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <pthread.h>
#include <iostream>
using namespace std;

// 检查参数是否为空格
#define ISsapce(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttp/0.1.0\r\n"   // 定义server名称
#define STDIN 0
#define STDOUT 1
#define STDERR 2

// 函数声明
void* accept_request(void*);
void bad_request(int);
void cat(int, FILE*);
void cannot_execute(int);
void error_die(const char*);
void execute_cgi(int, const char*, const char*, const char*);
int get_line(int, char*, int);
void headers(int, const char*);
void not_found(int);
void server_file(int, const char*);
int startup(uint16_t*);
void unimplemented(int);

// 处理从套接字上监听到的http请求
void* accept_request(void* arg) {
    int client = *(int*)arg;
    char buf[1024];    
    char method[255];    // 请求方法
    char url[255];
    char path[512];

    struct stat st;
    int cgi = 0;      // 用于标记是否需要执行cgi程序

    char* query_string = NULL;
    size_t numchars = get_line(client, buf, sizeof(buf));
    size_t i = 0, j = 0;
    // 提取其中的请求方式
    while(!(ISsapce(buf[i])) && (i < sizeof(method) -1 )) {
        method[i] = buf[i];
        ++i;
    }

    j = i;
    method[i] = '\0';

    // 如果请求方法不为GET和POST，将client设置为未实施的
    if(strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        unimplemented(client);   
        return NULL;
    }

    // 如果请求方法为POST，则需要执行cgi脚本
    if(strcasecmp(method, "POST") == 0) {
        cgi = 1;
    }
    i = 0;

    // 跳过所有的空格
    while(ISsapce(buf[i]) && (j < numchars)) {
        ++j;
    }


    // 将url读取出来放到url数组中
    while(ISsapce(buf[j]) && (i < sizeof(url) - 1) && (j < numchars)) {
        url[i] = buf[j];
        ++i;
        ++j;
    }

    url[i] = '\0';

    // 如果请求方法为GET，url可能会带有?,含参查询
    if(strcasecmp(method, "GET") == 0) {
        query_string = url;
        while((*query_string != '?') && (*query_string != '\0')) {
            ++query_string;
        }
            // 如果是含参查询，需要执行cgi脚本解析参数，将cgi标志位设置为1
            if(*query_string == '?') {
                
                cgi = 1;
                // 将解析的参数进行截取
                *query_string = '\0';
                ++query_string;
            }
    }

        // 将url中的路径格式化到path中
        sprintf(path, "htdocs%s", url);
        // 如果path只是一个目录，默认显示index.html
        if(path[strlen(path) - 1] == '/') {
            strcat(path, "index.html");
        }

        // 如果当前路径不是指定路径
        if(stat(path, &st) == -1) {

            // 档numchars > 0 并且 buf没有走到\n就一直读同时丢弃header
            while((numchars > 0) && strcmp("\n", buf)) {
                numchars = get_line(client, buf, sizeof(buf));

            }
            // 告诉客户端网页不存在
            not_found(client);
        } else {
            // 如果访问的主页存在将主页进行显示
            // S_IFDIR代表目录
            if((st.st_mode & S_IFMT) == S_IFDIR) {
                strcat(path, "index.html");
            }

            // S_IXUSR:文件所有者具有可执行权限
            // S_IXGRP：用户组就有可执行权限
            // S_IXOTH：其他用户具有可读权限
            if((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH)) {
                cgi = 1;
            }

            if(!cgi) {
                // 返回静态文件
                server_file(client, path);
            } else {
                // 执行cgi脚本
                execute_cgi(client, path, method, query_string);
            }

        }

        close(client);    // 由于http是面向无连接的所以需要关闭
        return NULL;

}


// 将错误请求400返回给客户端
void bad_request(int client) {
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAR REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);

    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);

    sprintf(buf, "Your browser send a bad request,");
    send(client, buf, sizeof(buf), 0);

    sprintf(buf, "such as a POST without a Content-length.");
    send(client, buf, sizeof(buf), 0);
}

// 读取服务器上某个文件写到socket套接字
// 将文件的全部内容当道一个套接字上
void cat(int client, FILE* resource) {
    char buf[1024];

    // 将从文件流中读取到的数据放入buf中
    fgets(buf, sizeof(buf), resource);
    // 只要文件流中有数据就一直读
    while(!feof(resource)) {
        // 将读到的数据发送给客户端
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

// 处理发生在cgi程序时出现的错误
// 通知客户端无法执行当前的CGI脚本
void cannot_execute(int client) {
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

// 此函数用于打印错误信息，同时退出进程
void error_die(const char* str) {
    perror(str);
    exit(1);
}

// 运行cgi脚本，将环境变量设置为 *appropriate.
void execute_cgi(int client, const char *path, const char *method, const char *query_string) {
    char buf[1024];
    // 声明读写管道，一个用于接收数据，一个用于发送数据
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    
    buf[0] = 'A'; 
    buf[1] = '\0';

    int numchars = 1;    // 用于接收从套接字中按行读取到的数据长度
    int content_length = -1;   // 用于接收buf中的数据长度

    // 如果请求方法为GET
    if(strcasecmp(method, "GET") == 0) {

        // 只要numchars大于0 并且buf中还有数据
        while((numchars > 0) && strcmp("\n", buf)) {

            // 就从套接字中按行读取信息，读取完之后丢弃头信息
            numchars = get_line(client, buf, sizeof(buf));
        }
    } else if(strcasecmp(method, "POST") == 0) {

        numchars = get_line(client, buf, sizeof(buf));

        // 循环读取头信息找到Content-Length字段的值
        while((numchars > 0) && strcmp("\n", buf)) {
            buf[15] = '\0';   // 用于截取Content-Length
            // 获取Conntent-Length的值
            if(strcasecmp(buf, "Content-Length:") == 0) {
                content_length = atoi(&(buf[16]));
            }

            numchars = get_line(client, buf, sizeof(buf));
        }

        // 如果buf中的长度为-1
        if(content_length == -1) {
            // 告诉客户端这是个错误的请求
            bad_request(client);
            return;
        }
    } else {

    }

    // 必须在frok()中调用pipe(),否则子进程不会集成文件描述符
    // 使用cgi_output创建匿名管道读取cgi程序，
    // 若创建失败向客户端通知处理cgi脚本失败
    if(pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }

    // 使用cgi_input创建匿名管道
    if(pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }

    // 创建进程
    if((pid = fork()) < 0) {
        cannot_execute(client);
        return;
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    // 如果是子进程就让它去执行cgi程序
    if(pid == 0) {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], STDOUT);    // 将cgi_output[2]重定向为标准输出
        dup2(cgi_input[0], STDIN);   // 将cgi_inout[0] 重定向为标准输入

        // pipe 中 0 为读端  1 为写端
        // 关闭输出的读端，关闭输入的写端
        close(cgi_output[0]);
        close(cgi_input[1]);

        // 将请求方法存图环境变量中，进行cgi脚本交互
        sprintf(meth_env, "REQUEST_METHOD = %s", method);
        // 修改环境变量为meth_env
        putenv(meth_env);   

        // 如果请求方法为GET
        if(strcasecmp(method, "GET") == 0) {
            // 获取查询字符串存入查询环境变量中
            sprintf(query_env, "QUERY_STRING = %s", query_string);
            putenv(query_env);   // 存储query_env
        } else {
            // 如果请求方法为POST，获取内容长度
            sprintf(length_env, "CONTENT_LENGTH = %d", content_length);
           // 存储CONTENT_LENGTH
            putenv(length_env);
        }
        // 执行path下的文件
        execl(path,path,  NULL);
        exit(0);

    } else {
        // 如果是父进程
        // 关闭cgi_output的写端，关闭cgi_input的读端
        close(cgi_output[1]);
        close(cgi_input[0]);

        // 如果请求方法为post
        if(strcasecmp(method, "POST") == 0) {
            for(i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);   // 从客户端中一个字节一个字节的接收数据
                write(cgi_input[1], &c, 1);   // 将从客户端接受到的数据一个字节一个字节的写入到cgi_input

            }

        }
		
		while(read(cgi_output[0], &c, 1) > 0) {
            // 将cgi脚本的返回数据发送给浏览器客户端
                send(client, &c, 1, 0);
            }

            // 关闭读端、写端
            close(cgi_output[0]);
            close(cgi_input[1]);

            // waitpid()暂时停止目前进程的执行，
            // 直到有信号到来或者子进程结束
            // 如果在调用wait()时子进程已经结束，
            // 则wait()会立即返回子进程结束状态值
            // 子进程的结束状态值会由参数status 返回，
            // 而子进程的进程识别码也会一块儿返回
            waitpid(pid, &status, 0);

    }

    
}

// 读取套接字的一行，把回车换行等情况统一为换行符结束
int get_line(int sock, char *buf, int size) {
    int i = 0;
    char c = '\0';
    int n;

    while((i < size-1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0);

        if(n > 0) {
            if(c == '\r') {
                // 把flags设置为MSG_PEEK，
                // 仅把tcp buffer中的数据读取到buf中，
                // 并不把已读取的数据从tcp buffer中移除，
                // 再次调用recv仍然可以读到刚才读到的数据
                n = recv(sock, &c, 1, MSG_PEEK);

                if((n > 0) && (c == '\n')) {
                   recv(sock, &c, 1, 0);
                } else {
                    c = '\n';
                }
            }

            buf[i] = c;
            ++i;
        } else {
            c = '\n';
        }
    }
    buf[i] = '\0';

    return (i);
}

// 将http响应的头部写到套接字中
// 处理200 OK
void headers(int client, const char *filename) {
    char buf[1024];
    (void)filename;     // 无法通过文件名确定文件类型

    // http状态码200的响应信息发送给客户端
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "Content-Type:text/html\r\n");
    send(client, buf, strlen(buf), 0);

    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

//主要处理找不到请求的文件时的情况
//处理404 page not found
void not_found(int client) {
    char buf[1024];

    sprintf(buf, "HTPP/1.0 404 PAGE NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "<HTML><TITLE>Page Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "your request because the resource specfied\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "is unavailable or nonexisetent.\r\n");
    send(client, buf, strlen(buf), 0);

    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
    
}

// 调用cat把服务器文件返回给浏览器客户端
// 向客户机发送常规文件。使用报头，
// 并在出现错误时向客户机报告错误。
void server_file(int client, const char *filename) {
    FILE* resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A';
    buf[1] = '\0';

    // 读取http头部，读取完之后丢弃
    while((numchars > 0) && strcmp("\n", buf)) {
        numchars = get_line(client, buf, sizeof(buf));
    }

    // 以制度方式打开文件
    resource = fopen(filename, "r");
    if(resource == NULL) {
        // 如果文件为空，给客户端发送404错误信息
        not_found(client);
    } else {
        // 添加http头
        headers(client, filename);
        // 将文件内容发送给客户端
        cat(client, resource);
    }

    // 关闭文件句柄
    fclose(resource);
}

// 初始化套接字
int startup(uint16_t* port) {
    int http = 0;
    int on = 1;
    struct sockaddr_in name;

    http = socket(AF_INET, SOCK_STREAM, 0);
    if(http == -1) {
        error_die("socket error");
    }

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    
    // 为套接字http设置选项值
    if((setsockopt(http, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0) {
        error_die("setsockopt failed");
    }

    // 为套接字绑定地址信息
    if(bind(http, (struct sockaddr*)&name, sizeof(name)) < 0) {
       error_die("bind error");
    }

    // 动态分配端口
    if(*port == 0) {
        socklen_t namelen = sizeof(name);

        // 用于获取http的名字，用于一个已绑定地址信息的套接字
        if(getsockname(http, (struct sockaddr*)&name, &namelen) == -1) {
            error_die("getsockname error");
        }

        *port = ntohs(name.sin_port);
    }

    // 监听连接，最大连接数为5
    if(listen(http, 5) < 0) {
        error_die("listen error");
    }

    return http;

}

// 返回给浏览器表名收到的http请求所用的方法不被支持
void unimplemented(int client) {
	char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
   
	sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

int main(int argc, char* argv[]) {
    int server_sock = -1;
    uint16_t port = 0;
    int client_sock = -1;
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);
    pthread_t newpthread;

    // 初始化服务端套接字
    server_sock = startup(&port);
    cout << "http running on port:" << port << endl;

    while(1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client, &client_len);
        if(client_sock == -1) {
            error_die("accept error");
        }

        // 启动线程处理新连接
        if(pthread_create(&newpthread, NULL, accept_request, (void*)&client_sock) != 0) {
            perror("pthread_create error");
        }

    }
    // 关闭服务端套接字
    close(server_sock);
    return 0;
}
