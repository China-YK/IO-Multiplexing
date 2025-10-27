//
// Created by yk on 2025/10/27.
//
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
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

    fd_set reads,copy_reads;
    //简直恐怖
    //fd_set本质是一个定长位图数组，里面unsinged long fds_bits[__FD_SETSIZE / __NFDBITS];
    //实质上是一个unsinged long fds_bits[16];一共占8 * 8 *16 = 1024位
    //数组下标	  表示的 fd 范围	  内部存储的比特
    //fds_bits[0]     0–63         bit0~bit63
    //fds_bits[1]     64–127       bit0~bit63
    //也就是说fds_bits[0]=1时，0-63的fd只有第0个fd被放入监听中，如果fds_bits[0]=3那就表示第0个和第1个都被放入监听，若fds_bits[0]=2，那就表示第1个被放入监听
    //真的nb啊，如果是我的话就就直接开一个1024大小的int数组，nums[k]=1就表示第k个fd被放入监听
    //它这里只花了1024个比特位就实现了监听1024个fd，每个比特位为0表示没有监听，为1放入监听，这就是内核级优化！

    FD_ZERO(&reads);
    //将1024个比特位都置为0
    FD_SET(sock_server,&reads);
    //将fd对于的比特位置为1(从0开始计数)，比如sock_server=38(fd从0开始计数)，那就将第38个比特位置为1--->fds_bits[0]+=2^38
    timeval timeout = {0,500000};
    int max_sock = sock_server;
    while (true){
        copy_reads = reads;
        int fd_num = select(max_sock + 1,&copy_reads,nullptr,nullptr,&timeout);
        //先总结一句话，返回我们所监听的事件里面有多少事件触发了可读/可写/异常
        //第一个参数：max_sock+1因为文件描述符是从 0 开始编号的。比如最大 fd 是 5，那么你得检查 0~5 共 6 个描述符。
        //第二个参数：监听可读集合，我们从第0位一直到max_sock位遍历，找出比特位为1的(放入监听集合的fd)，然后内核再去看这些fd的缓冲区是否有非空，如果有数据、或者连接被关闭，就认为“可读”。
        //第三个参数：同样的，只是我们是监听可写集合
        //第四个参数：监听异常集合
        //第五个参数：设置超时。select有两个阶段
        //阶段 1：立即扫描所有 fd 的状态（同步检查）内核遍历你关心的所有 fd； 检查它们是否已经“可读/可写”； 这个检查是立刻完成的（微秒级），不会阻塞。
        //阶段 2：如果都没事件 → 进入睡眠等待这时才会进入“阻塞状态”；最多等待 timeout 这么久； 若期间任何 fd 状态改变（可读、可写），立刻唤醒； 若超时到了，还没事件 → 返回 0。
        //-----再讲一下为什么这里要传入copy_reads
        //因为经历select之后，里面比特位01的含义就变了，之前是1表示这个fd被放入监听，0表示这个fd没有放入监听
        //这里的1表示对应fd是否有事件发生，0表示对应fd没有事件发生
        //所以我们这里要传入copy_reads

        if (fd_num <= -1)handler("select is error",errno);
        if (fd_num == 0)continue;//没有事件发生就继续循环监听
        for (int i = 0;i <= max_sock;i++){
            if (FD_ISSET(i,&copy_reads)){//判断第copy_reads的第i位是否为1，为1就返回真
                //经过select之后的01就变了含义，为1就说明第i个fd有事件发生，这里表示可读事件发生
                if (i == sock_server){//如果这个i是sock_server这个fd文件描述符，那表示有客户端连接
                    sock_client = accept(sock_server,(struct sockaddr*)&client,&client_len);
                    FD_SET(sock_client,&reads);//将新客户端 fd加入主监听集合，下一轮开始就会被 select 关注。
                    if (max_sock < sock_client)max_sock = sock_client;//更新一下边界
                    fprintf(stdout,"client is connected:%d\n",sock_client);
                }
                else{//如果不是服务器的socket那就只有是客户端有可读事件发生了
                    char buffer[512];
                    memset(buffer,0,sizeof(buffer));
                    ssize_t len = read(i,buffer,sizeof(buffer));
                    if (len == 0)//客户端关闭时，也会结束read的阻塞！
                    {
                        FD_CLR(i,&reads);//len=0表示客户端关闭连接，我们这里也要更新一下reads，将对应的位置为0，结束对它的监听
                        close(i);
                        fprintf(stdout,"client is disconnected:%d\n",i);
                    }else{
                        write(i,buffer,len + 1);//将'\0'也发送
                    }
                }
            }
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
        // fprintf(stdout,buffer);
        //防止格式化字符串攻击，因为buffer一般都是用户输入之类的，如果用户故意输入hello %s %s %s %n
        //那么 fprintf(stdout, buffer) 就会把这些 %s、%n 当作格式指令，试图从栈上取数据，甚至改写内存地址。
        fprintf(stdout,"%s",buffer);
    }
    close(sock_client);
}
void TestSelect(int argc,char* args[]){
    if (argc != 3 && argc != 4)handler("Useage is failed!\n",errno);
    if (argc == 3)CreateServer(argc,args);
    else CreateClient(argc,args);
}
int main(int argc,char* args[]){
    TestSelect(argc,args);
    return 0;
}