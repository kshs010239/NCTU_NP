#include<sys/socket.h>
#include<stdio.h>
#include<sys/types.h>
#include<stdlib.h>
#include<unistd.h>
#include<strings.h>
#include<string.h>
#include<netinet/in.h>
#include<sys/wait.h>
#include<errno.h>
#include<signal.h>
#define SERV_TCP_PORT 1256
#define MAXLINE 10000

void err_dump(const char* s)
{
	perror(s);
	abort();
	exit(1);
}

ssize_t writen (int fd, const char *buf, size_t num)
{
	ssize_t res;
	size_t n = num;
	const char *ptr = buf;
	while (n > 0) 
	{
    	if ((res = write (fd, ptr, n)) <= 0)
	        return (-1);
    	ptr += res; 
    	n -= res;
	}
	return(num);
}

int readline(int fd, char * ptr, int maxlen)
{
	int n, rc; 
	char c;
	for (n = 1; n < maxlen; n++) 
	{
		if ( (rc = read(fd, &c, 1)) == 1) 
		{
			*ptr++ = c;
			if (c == '\n') break;
		} 
		else if (rc == 0) 
		{
			if (n == 1) return(0); /* EOF, no data read */
  			else break; /* EOF, some data was read */
		} 
		else
			return(-1); /* error */
	}
	*ptr = 0;
	return(n);
}

extern char ** environ;
int do_env(char* argv[])
{
	if(strcmp("exit", argv[0]) == 0)
		exit(0);
	if(strcmp("setenv", argv[0]) == 0)
	{
		if(argv[1] == 0)
			for(int i = 0; environ[i] != 0; i++)
				printf("%s\n", environ[i]);
		else
			setenv(argv[1], argv[2], 1);
		fflush(stdout);
		return 1;
	}
	if(strcmp("printenv", argv[0]) == 0)
	{
		if(argv[1] == 0)
			for(int i = 0; environ[i] != 0; i++)
				printf("%s\n", environ[i]);
		else
			for(int i = 1; argv[i] != 0; i++)
			{
				char* env_get = getenv(argv[i]);
				if(env_get != 0)
					printf("%s=%s\n", argv[i], env_get);
			}
		fflush(stdout);
		return 1;
	}
	return 0;
}

int do_command(char* argv[], int in_fd, int out_fd)
{
	int status = 0;
	int return_pid;
	int exec_pid = fork();
	if(exec_pid < 0)
		err_dump("exec: fork error");
	else if(exec_pid == 0)
	{
		if(out_fd != -1)
			dup2(out_fd, STDOUT_FILENO);
		if(in_fd != -1)
			dup2(in_fd, STDIN_FILENO);
		execvp(argv[0], argv);
		fprintf(stderr, "Unknown command: [%s].\n", argv[0]);
		exit(1);
	}
	else
		return_pid = waitpid(exec_pid, &status, 0);
	//printf("%d %d %d\n",exec_pid, return_pid, status);
	//fflush(stdout);
	return status;
}

int security(char line[])
{
	for(char* c = line; *c != 0; ++c)
		if(*c == '/')
			return 1;
	return 0;
}

void try_close(int* fd)
{
	if(*fd == -1) return;
	close(*fd);
	*fd = -1;
}

void socket_shell(int sockfd)
{
	if(dup2(sockfd, STDOUT_FILENO) < 0)
		err_dump("dup2 stdout error");
	if(dup2(sockfd, STDERR_FILENO) < 0)
		err_dump("dup2 stderr error");
	writen(sockfd,	
			"****************************************\n"
			"** Welcome to the information server. **\n"
			"****************************************\n",
			123);
	char * env_default[] = {"PATH=bin:.", (char*)NULL};
	environ = env_default;
	int n;
	char line[MAXLINE];
	char* argv[MAXLINE];
	int pfd[MAXLINE][2];
	for(int i = 0; i < MAXLINE; i++)
		pfd[i][0] = pfd[i][1] = -1;
	int pipe_cnt = 0;
	for(;;) 
	{
		writen(sockfd, "% ", 2);
		n = readline(sockfd, line, MAXLINE);
		if (n == 0) return; /* connection terminated */ 
		else if (n < 0) 
			err_dump("socket_shell: readline error");
		if(security(line))
		{
			fputs("illegal character: '/'.\n", stderr);
			continue;
		}
		int argc = 0;
		char delimiter[] = {' ', '\n', 13};
		argv[argc] = strtok(line, delimiter);
		while(argv[argc] != 0)
			argv[++argc] = strtok(NULL, delimiter);
		if(do_env(argv)) // printenv and setenv
			continue;
		int beg = 0;
		for(int i = 0; i < argc; i++)
		{
			if(argv[i][0] != '>' && argv[i][0] != '|')
				continue;
			FILE* fout;
			int pipe_num;
			if(argv[i][0] == '>')
			{
				argv[i] = 0;
				fout = fopen(argv[++i], "w");
				do_command(argv + beg, pfd[pipe_cnt][1], fileno(fout));
				fclose(fout);
			}
			else if(argv[i][0] == '|')
			{
				pipe_num = pipe_cnt + (argv[i][1] == 0 ? 1 : atoi(argv[i] + 1));
				pipe_num = pipe_num > 1000 ? pipe_num - 1001 : pipe_num;
				if(pfd[pipe_num][0] == -1) // if pipe not open, open it.
					pipe(pfd[pipe_num]);
				argv[i] = 0;
				int wat;
				if((wat =do_command(argv + beg, pfd[pipe_cnt][1], pfd[pipe_num][0])) != 0)
				{
					argv[beg] = 0;
					break;
				}
				//printf("wat = %d\n", wat);

			}
			try_close(&pfd[pipe_cnt][1]);
			beg = i + 1;
			pipe_cnt = pipe_cnt + 1 > 1000 ? 0 : pipe_cnt + 1;
			try_close(&pfd[pipe_cnt][0]);
		}
		if(argv[beg] != 0) // final command, if there is.
		{
			if(do_command(argv + beg, pfd[pipe_cnt][1], -1) == 256)
			{
				pipe_cnt = pipe_cnt + 1 > 1000 ? 0 : pipe_cnt + 1;
				try_close(&pfd[pipe_cnt][0]);
				continue;
			}
			try_close(&pfd[pipe_cnt][1]);
			pipe_cnt = pipe_cnt + 1 > 1000 ? 0 : pipe_cnt + 1;
			try_close(&pfd[pipe_cnt][0]);
		}
	}
}

int main(int argc, char* argv[])
{
 	//struct sigaction sa;
	//sa.sa_handler = SIG_DFL;
  	//sa.sa_flags = SA_NOCLDWAIT;
	//sigaction(SIGCHLD, &sa, NULL);
	chdir("/net/cs/104/0416092/ras");
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
	listen(sockfd, 5);
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
			socket_shell(newsockfd);
			exit(0);
		}
		wait(NULL);
		close(newsockfd); /* parent process */


	}
}
