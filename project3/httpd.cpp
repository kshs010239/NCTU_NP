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
#define SERV_TCP_PORT 1258
#define bufsize 2048

char root[] = "/u/cs/104/0416092/public_html/";
char fullpath[bufsize];

char *path;
char *qstring;

void http_handle(int);
void read_request(int);
void set_env(int);
void log(char*);
void err_dump(const char*);



int main(int argc, char* argv[])
{
	struct sigaction sa; 
    sa.sa_handler = SIG_DFL;
    sa.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa, NULL);

	if(chdir(root) == -1)
		err_dump("chdir");
	int sockfd, newsockfd, childpid;
	socklen_t clilen;	
	struct sockaddr_in cli_addr, serv_addr;

	char* pname = argv[0];
	if((sockfd=socket(AF_INET,SOCK_STREAM,0)) < 0)
		err_dump("server: can't open socket");
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(SERV_TCP_PORT);
	if(bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
		err_dump("server: can't bind local address");

	listen(sockfd, 30);
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
			http_handle(newsockfd);
			exit(0);
		}
		close(newsockfd); /* parent process */
	}
}

void http_handle(int sockfd)
{
	log("handle start");
	read_request(sockfd);
	set_env(sockfd);
	char buffer[bufsize];
	sprintf(buffer, "HTTP/1.0 200 OK/r/n");
	write(sockfd, buffer, strlen(buffer));
	log("write finish");
	if(access(fullpath, F_OK | X_OK) != -1)
	{
		log(path);
		execl(fullpath, path, NULL);
		err_dump("exec");
	}
	else
	{
		log("not excutable");
		FILE* fin = fopen(fullpath, "r");
		if(fin == NULL)
			err_dump("fopen");
		char buf[bufsize + 1];
		printf("Content-Type: text/html\n\n");
		
		while(fgets(buf, sizeof(buf), fin))
			puts(buf);
	}
}


void read_request(int sockfd)
{
	int ret;
	static char buffer[bufsize + 1];
	ret = read(sockfd,  buffer, bufsize);
	if(ret <= 0)
		err_dump("read");
	buffer[ret] = 0;
	//log(buffer);
	
	char *p = strtok(buffer, " \n\r?");
	if(strcmp(p, "GET") && strcmp(p, "get"))
		exit(0);
	
	path = strtok(NULL, " \n\r?");
	if(path == NULL)
		exit(0);
	// check ..
	for(char* it = path + 1; *it != 0; it++)
		if(*it == '.' && *(it - 1) == '.')
			exit(0);
	path += 1;
	log(path);

	qstring = strtok(NULL, " \n\r?");	
	log(qstring);

	strcpy(fullpath, root);
	strcat(fullpath, path);
	log(fullpath);
	
}

void set_env(int sockfd)
{
	setenv("QUERY_STRING", 		qstring, 	1);
	setenv("CONTENT_LENGTH", 	"0",		1);
	setenv("REQUEST_METHOD", 	"GET", 		1);
	setenv("SCRIPT_NAME", 		"", 		1);
	setenv("REMOTE_HOST",		"",			1);
	setenv("REMOTE_ADDR",		"",			1);
	setenv("AUTH_TYPE",			"",			1);
	setenv("REMOTE_USER",		"",			1);
	setenv("REMOTE_IDENT",		"",			1);
}

void log(char s[])
{
	fprintf(stderr, "[httpd] %s\n", s);
	fflush(stdout);
}

void err_dump(const char* s)
{
	perror(s);
	abort();
	exit(1);
}
