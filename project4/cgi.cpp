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
#include <netdb.h>
#define SERV_TCP_PORT 1255
#include "socks4.h"
char qstring[1024];


typedef struct _host {
	char*				name;
	struct sockaddr_in 	sin;
	int 				sock;
	FILE*				fin;
	int					stat;
	int					socks;
	Request				request;
	char*				socks_name;
	// stat 0: not connected, 1: not writable, 2: writable 
} Host;

Host serv[6];
fd_set rfds, afds;

void log(const char []);
void err_dump(const char*);
int exist(Host*);
int exist_any();
void rm_host(Host*);


struct in_addr get_ip(char* hostname)
{
	struct in_addr ip;
	struct hostent *tmp = gethostbyname(hostname);
	if(tmp == NULL)
	{
		log("get_hostname fail");
	}
	memcpy(&ip, tmp->h_addr_list[0], tmp->h_length);
	return ip;
}

int add_host(Host* host, char* hostname, char* port, char* file)
{
	log(hostname);
	log(port);
	log(file);
	struct hostent *ip;
	memset(&host->sin, 0, sizeof(host->sin));
	host->sin.sin_family = AF_INET;
	host->sin.sin_port = htons ((uint16_t) atoi (port));
	if (ip = gethostbyname (hostname))
		memcpy(&host->sin.sin_addr, ip->h_addr_list[0], ip->h_length);
	else
		err_dump("gethostbyname");
	
	if ((host->sock = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
		fputs ("error: failed to build a socket\n", stderr);
		return -1;
	}
//	fcntl(host->sock, F_SETFL, O_NONBLOCK);

	if ((host->fin = fopen (file, "r")) == NULL) {
		log("file open error");
		fprintf (stderr, "error: failed to open file '%s'\n", file);
		return -1;
	}
	FD_SET (fileno(host->fin), &afds);	

	host->stat = 0;
	host->name = hostname;
}

void send_request(Host* host)
{
	log("send_request");
	host->request.vn = 4;
	host->request.cd = 1;
	for(int i = 0; i < 9; i++)
		fprintf(stderr, "%x ", ((unsigned char*)&host->request)[i]);
	fputs("\n", stderr);
	write(host->sock, &host->request, 8);
	write(host->sock, "\0", 1);
	log("send_request exit");
}

int recv_reply(Host* host)
{
	log("recv_reply");
	unsigned char reply[8];
	read(host->sock, reply, 8);
	if(reply[1] != 90)
		return -1;
	log("recv_reply exit");
	return 0;
}


void log(const char s[])
{
    fprintf(stderr, "[cgi] %s\n", s);
    fflush(stderr);
    //fflush(stdout);
}

void err_dump(const char* s)
{
    perror(s);
    abort();
    exit(1);
}

void compile(char*);

void resolve_qstring()
{
	static char* host[6], *port[6], *file[6], *sh[6], *sp[6];
	for(int i = 1; i < 6; i++)
		host[i] = port[i] = file[i] = sh[i] = sp[i] = NULL;
	strcpy(qstring, getenv("QUERY_STRING"));
    log(qstring);
    /*if(strcmp(qstring + strlen(qstring) - 2, ".c") == 0)
        compile(qstring);*/
    //log(qstring + strlen(qstring));

	char* argv[100];
	int cnt = 0;
	argv[0] = strtok(qstring, "&");
	while(argv[cnt] != NULL)
		argv[++cnt] = strtok(NULL, "&");
	for(int i = 0; argv[i] != NULL; i++)
	{
		char **arg;
		switch(argv[i][0])
		{
		case 'h':
			arg = host;
			break;
		case 'f':
			arg = file;
			break;
		case 'p':
			arg = port;
			break;
		case 's':
			argv[i] += 1;
			if(argv[i][0] == 'h')
				arg = sh;
			else
				arg = sp;
			break;
		default:
			arg = NULL;
		}
		if(arg == NULL)
			continue;
		int n = atoi(argv[i] + 1);
		if(n > 0 && n < 6)
		{
			char *p = argv[i] + 2;
			while(*p != '=');
			p += 1;
			arg[n] = p;
		}
		else
			log("n error");
	}
	log(sh[1]);
	log(sp[1]);
	for(int i = 1; i < 6; i++)
	{
		int is_socks = sh[i] != NULL && sh[i][0] != NULL && sp[i] != NULL & sp[i][0] != NULL;
		if(host[i] == NULL || port[i] == NULL || file[i] == NULL)
			continue;
		if(host[i][0] == NULL || port[i][0] == NULL || file[i][0] == NULL)
			continue;
		log("add_host");
		if(is_socks)
		{
			log("proxy cgi");
			if(add_host(serv + i, sh[i], sp[i], file[i]) == -1)
			{
				log("socks add_host error");
				continue;
			}
			serv[i].socks_name = host[i];
			serv[i].request.dst_port = ntohs(atoi(port[i]));
			serv[i].request.dst_ip = get_ip(host[i]);
			serv[i].socks = 2;
		}
		else
			if(add_host(serv + i, host[i], port[i], file[i]) == -1)
				log("add_host error");
	}
}


void print_header()
{
	char header[65536];
	char header1[] =
			"Content-Type: text/html\n\n"
			"<html>"
			"<head>"
			"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />"
			"<title>Network Programming Homework 3</title>"
			"</head>"
			"<body bgcolor=#336699>"
			"<font face=\"Courier New\" size=2 color=#FFFF99>"
			"<table width=\"800\" border=\"1\">"
			"<tr>"
		;
	strcpy(header, header1);


	for(int i = 1; i < 6; i++)
	{
		if(serv[i].fin == NULL)
			continue;
		char tmp[1024];
		if(serv[i].socks)
			sprintf(tmp, "<td>%s</td>", serv[i].socks_name);
		else
			sprintf(tmp, "<td>%s(%s)</td>", serv[i].name, inet_ntoa(serv[i].sin.sin_addr));
		strcat(header, tmp);
	}

	char header3[] = 
			"<tr>"
		;
	strcat(header, header3);
	
	for(int i = 1; i < 6; i++)
	{
		if(serv[i].fin == NULL)
			continue;
		char tmp[1024];
		sprintf(tmp, "<td valign=\"top\" id=\"m%d\"></td>", i - 1);
		strcat(header, tmp);
		
	}

	char header5[] =
			"</table>"
			"</body>"
			"</html>"
		;
	strcat(header, header5);
	puts(header);
}

void print_line(int i, char * s)
{
	printf("<script>document.all['m%d'].innerHTML += \"<b>%s</b><br>\";</script>\n", i - 1, s);
}

void test_output()
{
	log("test_output");
	for(int i = 1; i < 6; i++)
	{
		if(!exist(serv + i))
			continue;
		char buf[1024];
		while(fgets(buf, sizeof(buf), serv[i].fin))
		{
			buf[strlen(buf) - 1] = 0;
			log(buf);
			print_line(i, buf);
			//usleep(100000);
		}
	}
}

void send_cmd(Host *host, int idx)
{
	log("send_cmd");
	char cmd[65536];

	char * p = fgets(cmd, sizeof(cmd), host->fin);
	if(feof(host->fin) && p == NULL)
		write(host->sock, "exit\n", 5);
	else
	{
		write(host->sock, cmd, strlen(cmd));
		strtok(cmd, "\r\n");
		print_line(idx, cmd);
	}
	host->stat = 1;
}

/*int readall(int sock, char* buf, int size)
{
    int total = 0;
    int n;
    while(n = read(sock, buf, size))
    {
        if(n <= 0)
            return total;
        total += n;
        buf += n;
        size -= n;
    }
    return total;
}*/

void receive(Host *host, int idx)
{
	log("receive");
	char buf[65536];
	int size = read(host->sock, buf, sizeof(buf));
	if(size == 0)
		rm_host(host);
	else
	{
		buf[size] = 0;
		char* p = strtok(buf, "\r\n");
		while(p != 0)
		{
			for(char * it = p; *it != 0; it++)
				if(*it == '%')
					host->stat = 2;
			print_line(idx, p);
			p = strtok(NULL, "\r\n");
		}
	}
}

void rm_host (Host *host)
{
	if (host->sock != 0) 
	{
		if (host->stat) 
		{
			FD_CLR (host->sock, &afds);
			close (host->sock);
		}
		FD_CLR (fileno (host->fin), &afds);
		fclose (host->fin);
		memset (host, 0, sizeof (Host));
	}
}

/*void compile(char* file)
{
    puts("Content-Type: text/plain\r\n\r\n");
    fflush(stdout);
    log("compile");
    if(fork())
        wait(NULL);
    else
    {
        log("gcc");
        execlp("gcc", "gcc", file, 0);
        err_dump("gcc");
        exit(0);
    }
    if(fork())
        wait(NULL);
    else
    {
        log("a.out");
        execlp("./a.out", "./a.out", 0);
        //execve("/net/cs/104/0416092/public_html/a.out", "a.out", "");
        //char* tmp[] = {"a.out", NULL};
        //chdir("/net/cs/104/0416092/public_html");
        //execvp("a.out", tmp);
        err_dump("a.out");
        exit(0);
    }
    exit(0);
    log("!");
}*/

int main()
{
	log("-----\n");
	int nfds = 100;
	struct timeval timeout = {0};
	FD_ZERO(&afds);
    log("test");
	resolve_qstring();
	
	print_header();

	//test_output();
	for(int i = 1; i < 6; i++)
		if(exist(serv + i))
			fprintf(stderr, "[cgi] host %d\n", i);
	while(exist_any())
	{	
		for(int i = 1; i < 6; i++)
		{
			Host* host = serv + i;
			if(!exist(host))
				continue;
			if(host->stat == 0)
			{
				fflush(stdout);
				log("try connect");
				if(connect(host->sock, (const struct sockaddr*)&host->sin, sizeof(host->sin)) < 0)
				{
					if(errno != EINPROGRESS)
						err_dump(">> connect");
				}
				else
				{
					log("connect");
					FD_SET(host->sock, &afds);
					host->stat = 1;	
					if(host->socks)
					{
						send_request(host);
						if(recv_reply(host) < 0)
						{
							log("reply rejected");
							rm_host(host);
						}
					}
				}
			}
			if(host->stat > 0)
			{
				memcpy(&rfds, &afds, sizeof(rfds));
				timeout.tv_sec = 0;
				timeout.tv_usec = 10000;

				if (select (nfds, &rfds, NULL, NULL, &timeout) < 0)
					err_dump("select");

				/*if(host->socks == 2)
				{
					//if(!FD_ISSET(fileno(host->fin), &rfds))
					//	continue;
					if(!FD_ISSET(host->sock, &rfds))
						continue;
					send_request(host);
					host->socks = 1;
				}
				else if(host->socks == 1)
				{
					if(!FD_ISSET(host->sock, &rfds))
						continue;
					if(recv_reply(host) < 0)
					{
						log("reply rejected");
						rm_host(host);
					}
					host->socks = 0;
				}
				else */if(host->stat == 2 && FD_ISSET(fileno(host->fin), &rfds))
					send_cmd(host, i);
				else if(FD_ISSET(host->sock, &rfds)) 
					receive(host, i);
				else
					log("no send receive");
				fflush(stdout);
			}
			usleep(100000);
		}
	}

	/*int sock, n;
	struct sockaddr_in fsin; 
    struct sockaddr_in addr;
	
	if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_dump("server: can't open socket");
	bzero((char *) &addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERV_TCP_PORT);


	int flag = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, O_NONBLOCK);
	if((n = connect(sock, &addr, sizeof(addr)) < 0) {
		if (errno != EINPROGRESS) return (-1);
	}
	fd_set rfds; 
	fd_set wfds; 
	fd_set rs; 
	fd_set ws; 
	int conn = 1;
	nfds = FD_SETSIZE;
	FD_ZERO(&rfds); FD_ZERO(&wfds); FD_ZERO(&rs); FD_ZERO(&ws);
	FD_SET(sock, &rs);
	FD_SET(sock, &ws);
	rfds = rs;wfds = ws;
	int statusA = F_CONNECTING;
	while (conn > 0) {
		memcpy(&rfds, &rs, sizeof(rfds)); memcpy(&wfds, &ws, sizeof(wfds));
		if ( select(nfds, &rfds, &wfds, (fd_set*)0, (struct timeval*)0) < 0 ) errexit();
		if (statusA == F_CONNECTING &&
			(FD_ISSET(sock, &rfds) || FD_ISSET(sock, &wfds)))
		{
			if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &n) < 0 ||
				error != 0) {
				// non-blocking connect failed
				return (-1);
			}
			statusA = F_WRITING;
			FD_CLR(sock, &rs);
			shell_init(sock);
		}
		else if (statusA == F_WRITING && FD_ISSET(sock, &wfds) ) {
			NeedWrite -= n;
			if (n <= 0 || NeedWrite <= 0) {
				// write finished
				FD_CLR(sock, &ws);
				statusA = F_READING;
				FD_SET(sock, &rs);
			}
		}
		else if (statusA == F_READING && FD_ISSET(sock, &rfds) ) {
			
			if (n <= 0) {
				// read finished
				FD_CLR(sock, &rs);
				statusA = F_DONE ;
				conn--;
			}
		}
	}*/
	log("cgi exit");
}

int exist(Host * it)
{
	return it->fin != NULL;
}

int exist_any()
{
	for(int i = 1; i < 6; i++)
		if(exist(serv + i))
			return 1;
	return 0;
}
