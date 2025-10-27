//
// Created by yk on 2025/10/27.
//
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sys/epoll.h>
void handler(const char* str,int Errno){
    fprintf(stderr,"%s",str);
    fprintf(stderr,"The Errno is %s",strerror(Errno));
    exit(-1);
}
void CreateServer(int argc,char* args[]){
    int sock_server,sock_client;
    struct sockaddr_in server,client;
    socklen_t client_len;
    sock_server = socket(PF_INET,SOCK_STREAM,0);
    if (sock_server == -1)handler("server_socket failed!\n",errno);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(args[1]);
    server.sin_port = htons(atoi(args[2]));
    int opt = 1;
    setsockopt(sock_server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(int));
    if (bind(sock_server,(struct sockaddr*)&server,sizeof(server)) == -1)handler("bind failed!\n",errno);
    if (listen(sock_server,5) == -1)handler("listen failed!\n",errno);

    epoll_event event;
    // typedef union epoll_data
    // {
    //     void *ptr;
    //     int fd;
    //     uint32_t u32;
    //     uint64_t u64;
    // } epoll_data_t;
    //
    // struct epoll_event
    // {
    //     uint32_t events;	//用来保存要监听的事件类别
    //     epoll_data_t data;	//设置对哪一个fd进行事件监听
    // } __EPOLL_PACKED;
    int epfd,event_cnt;
    //epfd就是一个文件描述符，本质对应的是一个内核epoll对象。这里和socket一样的
    //event_cnt用来保存后面一次wait有多少fd发生了事件
    epoll_event* all_events = new epoll_event[128];
    //用来存放后面一次wait触发事件的fd(一个个event对象)
    event.events = EPOLLIN;//设置为可读，如果要同时给sock_server设置多个事件监听应该event.events = EPOLLIN|EPOLLOUT;同时监听可读可写
    event.data.fd = sock_server;//对sock_server进行监听

    epfd = epoll_create(1);//创建一个epoll内核对象，并返回文件描述符epfd，这里和sock_server = socket(...)一样
    if (epfd == -1)handler("epoll_create is failed!\n",errno);//如果返回是-1那就表示出错
    epoll_ctl(epfd,EPOLL_CTL_ADD,sock_server,&event);
    // | 参数      | 作用                                                            |
    // | ------- | ---------------------------------------------------------------- |
    // | `epfd`  | epoll 实例的文件描述符（通过 `epoll_create()` 得到）                 |
    // | `op`    | 要对监控集合执行的操作类型（如 `EPOLL_CTL_ADD`放入、`EPOLL_CTL_DEL`删除、`EPOLL_CTL_MOD`） |
    // | `fd`    | 你希望 epoll **去监听的目标文件描述符**                               |
    // | `event` | 描述“你想监听哪些事件”的结构体（比如 EPOLLIN、EPOLLOUT 等）            |
    //这一句就相当于监听sock_server的可读事件

    while (true){

        event_cnt = epoll_wait(epfd,all_events,128,1000);
        // | 参数名         | 类型                    | 含义
        // | 第一个参数      | `int`                 | epoll 实例的文件描述符（由 `epoll_create()` 返回）
        // | 第二个参数    | `struct epoll_event*` | 用户提供的数组，用于**接收内核返回的已就绪事件**，并把这event_cnt个event存放到all_events的0~event_cnt-1的位置上
        // | 第三个参数 | `int`                 | 数组长度（一次最多返回多少个事件）
        // | 第四个参数   | `int`                 | 最长等待时间（毫秒）<br> - `>0`：超时时间<br> - `0`：立即返回（非阻塞）<br> - `-1`：无限阻塞，直到有事件
        //这里的timeout和select里面的机制一样，都是结束之后再额外等多久
        //返回值event_cnt就是本次有多少fd触发了事件

        if (event_cnt == -1)handler("epoll_wait is failed!\n",errno);
        if (event_cnt == 0)continue;//返回值为0，表示本次wait没有任何fd触发事件，进行下一轮wait
        for (int i = 0;i < event_cnt;i++){//对本次触发事件的fd进行处理
            if (all_events[i].data.fd == sock_server){//如果当前是服务端的可读事件触发
                client_len = sizeof(client);
                sock_client = accept(sock_server,(struct sockaddr*)&client,&client_len);
                if (sock_client == -1)handler("accept is failed!\n",errno);

                //给sock_client设置可读事件，并加入到内核epoll的监听中
                event.events = EPOLLIN;
                event.data.fd = sock_client;
                epoll_ctl(epfd,EPOLL_CTL_ADD,sock_client,&event);


                fprintf(stdout,"client is connected!%d\n",sock_client);
            }else{//客户端触发事件
                char buffer[512];
                memset(buffer,0,sizeof(buffer));
                ssize_t len = read(all_events[i].data.fd,buffer,sizeof(buffer));
                if (len <= 0){
                    //这个时候就要将对于的event从epoll对象监听中删去
                    epoll_ctl(epfd,EPOLL_CTL_DEL,all_events[i].data.fd,nullptr);

                    close(all_events[i].data.fd);
                    fprintf(stdout,"client is disconnected!%d\n",all_events[i].data.fd);
                }else{//否则的话就回声
                    write(all_events[i].data.fd,buffer,len);
                }
            }

            //这里讲一下，因为我们这个示例里面都是只设置了单个事件，可读事件，如果有多个事件监听，应该使用按位与
            if (all_events[i].events & EPOLLIN) {
                // ✅ 可读事件发生
            }
            if (all_events[i].events & EPOLLOUT) {
                // ✅ 可写事件发生
            }
            if (all_events[i].events & (EPOLLERR | EPOLLHUP)) {
                // ❌ 出错或挂起
            }
            //如果可读和可写事件同时触发，那么上面两个if都会执行！
        }
    }

}
void CreateClient(int argc,char* args[]){
    int sock_client;
    struct sockaddr_in server;
    socklen_t server_len = sizeof(server);
    sock_client = socket(PF_INET,SOCK_STREAM,0);
    if (sock_client == -1)handler("client_socket failed!\n",errno);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(args[1]);
    server.sin_port = htons(atoi(args[2]));
    int ret = connect(sock_client,(struct sockaddr*)&server,server_len);
    if (ret == -1){
        handler("connect failed!\n",errno);
        close(sock_client);
    }
    fprintf(stdout,"连接成功！\n");
    char buffer[512];
    while (true){
        memset(buffer,0,sizeof(buffer));
        fgets(buffer,sizeof(buffer),stdin);
        if (!strcmp(buffer,"q\n") || !strcmp(buffer,"Q\n"))break;
        int total = strlen(buffer) + 1;//将'\0'也发送
        int send = 0;
        while (send < total){
            ssize_t len = write(sock_client,buffer+send,total-send);
            send += len;
        }
        memset(buffer,0,sizeof(buffer));
        ssize_t len = read(sock_client,buffer,sizeof(buffer));
        if (len <= 0)break;
        fprintf(stdout,"%s",buffer);
    }
    close(sock_client);
}
void TestEpoll(int argc,char* args[]){
    if (argc != 3 && argc != 4)handler("Useage is failed!\n",errno);
    if (argc == 3)CreateServer(argc,args);
    else CreateClient(argc,args);
}
int main(int argc,char* args[]){
    TestEpoll(argc,args);
    return 0;
}