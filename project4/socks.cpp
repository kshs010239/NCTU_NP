#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <sys/shm.h>
#include <arpa/inet.h>
#include <map>
#include "socks4.h"
#define SERV_TCP_PORT 1259
#define bufsize 2048


void socks();
void log(const char*);
void err_dump(const char*);
int passiveTCP(int, int);
void recv_request();
void fire_wall(int);
int read_config(int);
void print_msg();
void send_reply();
int do_connect();
int do_bind();
void trans(int);

using namespace std;

Request request;
Reply reply;
struct sockaddr_in src;
int cur_sock;

int main(int argc, char* argv[])
{
	remove(".tmp");
	struct sigaction sa; 
    sa.sa_handler = SIG_DFL;
    sa.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa, NULL);

	int sockfd, newsockfd, childpid;
	socklen_t clilen;	
	//struct sockaddr_in cli_addr, serv_addr;
	struct sockaddr_in cli_addr;

	sockfd = passiveTCP(SERV_TCP_PORT, 5);
	for ( ; ; ) {
		clilen = sizeof(cli_addr);
		newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
		
		if (newsockfd < 0) err_dump("server: accept error");
		if ( (childpid = fork()) < 0) err_dump("server: fork error");
		else if (childpid == 0) /* child process */ 
		{
			/* close original socket */
			close(sockfd);
			/* process the request */
			dup2(newsockfd, STDOUT_FILENO);
			dup2(newsockfd, STDIN_FILENO);
			src = cli_addr;
			close(newsockfd);
			socks();
			exit(0);
		}
		close(newsockfd); /* parent process */
	}
}

void socks()
{
	reply.vn = 0;
	reply.cd = 90;
	recv_request();
	fire_wall(request.cd);
	//int dst = request.cd == 1 ? do_connect() : do_bind();
	int dst;
	if(request.cd == 1)
	{
		dst = do_connect();
		if(dst < 0)
		{
			log("connect fail");
			reply.cd = 91;
		}
		reply.dst_port = request.dst_port;
		reply.dst_ip = request.dst_ip;
	}
	else
	{
		dst = do_bind();
		if(dst < 0)
		{
			log("bind fail");
			reply.cd = 91;
		}
		reply.dst_ip.s_addr = 0;
	}
	send_reply();
	print_msg();
	if(reply.cd != 90)
		return;
	trans(dst);
}

void trans(int dst)
{
	log("trans");
	char buf[1048576];
	int nfds = dst + 1;
	fd_set afds, rfds;
	FD_ZERO(&afds);
	FD_SET(0, &afds);
	FD_SET(dst, &afds);
	for(;;)
	{
		memcpy(&rfds, &afds, sizeof(rfds));
		if(select(nfds, &rfds, 0, 0, 0) < 0)
			err_dump("select");
		if(FD_ISSET(0, &rfds))
		{
			int n = read(0, buf, sizeof(buf) - 1);
			if(n <= 0)
			{
				FD_CLR(0, &afds);	
				close(0);
				close(1);
				FD_CLR(dst, &afds);
				close(dst);
				break;
			}
			else
				write(dst, buf, n);
		}
		if(FD_ISSET(dst, &rfds))
		{
			int n = read(dst, buf, sizeof(buf) - 1);
			if(n <= 0)
			{
				FD_CLR(0, &afds);	
				close(0);
				close(1);
				FD_CLR(dst, &afds);
				close(dst);
				break;
			}
			else
			{
				write(1, buf, n);
				//write(2, buf, n);
			}
		}
	}
	log("trans exit");
}


int do_connect()
{
	log("do_connect");
	int dst;
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = request.dst_port;
	sin.sin_addr = request.dst_ip;
	dst = socket(PF_INET, SOCK_STREAM, 0);
	if(dst < 0)
	{
		log("socket fail");
		return -1;
	}
	log("connect");
	if(connect(dst, (const struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		log("connect fail");
		return -1;
	}
	log("do_connect exit");
	return dst;
}

int do_bind()
{
	log("do_bind");
	int dst, sock;
	struct sockaddr_in cli;
	socklen_t cli_size = sizeof(struct sockaddr_in);
	log("passiveTCP");
	sock = passiveTCP(0, 0);
	if(sock < 0)
		err_dump("passiveTCP");
	log("getsockname");
	getsockname(sock, (struct sockaddr*)&cli, &cli_size);
	reply.dst_port = cli.sin_port;
	memset(&reply.dst_ip, 0, 4);
	send_reply();
	
	cli_size = sizeof(struct sockaddr_in);
	log("accept");
	dst = accept(sock, (struct sockaddr*)&cli, &cli_size);
	if(dst < 0)
		err_dump("accept");
	log("do_bind exit");
	return dst;
}

void recv_request()
{
	read(0, &request, 8);
	//memcpy(&request.dst_ip, buf + 4, 4);
	request.dst_port = request.dst_port;
	//fprintf(stderr, "[socks] %d %d %d %s\n", request.vn, request.cd,
	request.dst_port, inet_ntoa(request.dst_ip);
	if(request.vn != 4)
		err_dump("recv_request[vn]");
	if(request.cd != 1 && request.cd != 2)
		err_dump("recv_request[cd]");
	char c;
	while(read(0, &c, 1))
		if(c == '\0')
			return;
}
void send_reply()
{
	char * p = (char*) &reply;
	//for(int i = 0; i < 8; i++)
	//	fprintf(stderr, "%x ", p[i]);
	//fputs("", stderr);
	write(0, &reply, 8);
}

void fire_wall(int cd)
{
	int ban = read_config(cd);
	if(ban)
	{
		log("banned ip");
		reply.cd = 91;
		reply.dst_ip.s_addr = 0;
		reply.dst_port = 0;
		print_msg();
		send_reply();
		exit(0);
	}
}

int split(char* src, const char* pat, char**dst, int max)
{
	int argc = 0;
	dst[0] = strtok(src, pat);
	while(argc < max && dst[argc] != NULL)
		dst[++argc] = strtok(NULL, pat);
	return argc;
}

int read_config(int cd)
{
	log("read_config");
	int ban = 1;
	
	int dst_ip = request.dst_ip.s_addr;
	FILE* tmp = fopen(".tmp", "r");
	if(tmp != 0)
	{
		int hit = 0;
		while(1)
		{
			int tmp_ip;
			if(fscanf(tmp, "%d", &tmp_ip) <= 0)
				break;
			if(tmp_ip == dst_ip)
				hit += 1;
		}
		if(hit >= 3)
			return 1;
		fclose(tmp);
	}
	tmp = fopen(".tmp", "a");
	fprintf(tmp, "%d ", dst_ip);
		

	char* ip_src = inet_ntoa(request.dst_ip);
	fprintf(stderr, "ip_dst: %s\n", ip_src);
	char* ip[10];
	split(ip_src, ".", ip, 10);
	//*allow = (140 << 24) + (113 << 16);
	//*mask = 0xFFFF0000;
	FILE* conf = fopen("socks.conf", "r");
	while(1)
	{
		char line[1024];
		if(!fgets(line, sizeof(line), conf))
			break;
		char* argv[1024];
		int argc = split(line, " \t\n", argv, 1024);
		if(argc < 3)
			continue;
		if(strcmp(argv[0], "permit") != 0)
			continue;
		if(strcmp(argv[1], cd == 1 ? "c" : "b") != 0)
			continue;
		fprintf(stderr, "ip_conf: %s\n", argv[2]);
		argc = split(argv[2], ".", argv, 1024);
		if(argc < 4)
			continue;
		int i;
		for(i = 0; i < 4; i++)
		{
			if(strcmp(argv[i], "*") == 0)
				continue;
			if(strcmp(argv[i], ip[i]) != 0)
				break;
		}
		if(i == 4)
			ban = 0;
	}
	return ban;
}

void print_msg()
{
	fprintf(stderr, "<S_IP>\t:%s\n", inet_ntoa(src.sin_addr));
	fprintf(stderr, "<S_PORT>\t:%d\n", ntohs(src.sin_port));
	fprintf(stderr, "<D_IP>\t:%s\n", inet_ntoa(request.dst_ip));
	fprintf(stderr, "<D_PORT>\t:%d\n", ntohs(request.dst_port));
	fprintf(stderr, "<Command>\t: %s\n", request.cd == 1 ? "CONNECT" : "BIND");
	fprintf(stderr, "<Reply>\t: %s\n", reply.cd == 90 ? "Accept" : "Reject");
	unsigned char Content[100];
	unsigned char* p = (unsigned char*)&reply;
	sprintf((char*)Content, 
			"%x %x %x %x %x %x %x %x",
			 p[0],
			 p[1],
			 p[2],
			 p[3],
			 p[4],
			 p[5],
			 p[6],
			 p[7]
		);
	fprintf(stderr, "<Content>\t: %s\n", Content);
	fprintf(stderr, "\n");
	fflush(stderr);
}

void log(const char s[])
{
	fprintf(stderr, "[socks] %s\n", s);
	fflush(stderr);
}

void err_dump(const char* s)
{
	perror(s);
	abort();
	exit(1);
}

int passiveTCP (int port, int qlen)
{
	int			sockfd;
	struct sockaddr_in	serv_addr;

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		err_dump("socket");
 
	bzero (&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl (INADDR_ANY);
	serv_addr.sin_port = htons (port);

	if (bind (sockfd, (const struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		err_dump("bind");	

	listen (sockfd, qlen);

	return sockfd;
}
