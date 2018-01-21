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
#define SERV_TCP_PORT 1257
#define MAXLINE 10000
#define ID_NUM 30
#define ID_LIST_ADDR 0
#define INFO_BASE 4
#define INFO_LEN 50
#define PID_LEN 4
#define NAME_LEN 21
#define MES_BASE (INFO_BASE+INFO_LEN*(ID_NUM+1))
#define MES_LEN 1030
#define SHM_SIZE (MES_BASE+MES_LEN*(ID_NUM+1))
void pass(int i) {}
void debug(char s[])
{
	puts(s);
	fflush(stdout);
}


int id[1024];
int cur_id;
int id_to_fd[ID_NUM+1];
int* id_list;
char shm[SHM_SIZE];
int* pid[ID_NUM+1];
char* name[ID_NUM+1];
char* ip[ID_NUM+1];
char* mes[ID_NUM+1];
char fifo[ID_NUM+1][ID_NUM+1][10];
char* env[ID_NUM+1][100];
char tmp[1024];

int try_remove(char*);
int is_cmd(char[], char[]);
void bc(char[]);

void fifo_init()
{
	for(int i = 1; i <= ID_NUM; i++)
		for(int j = 1; j <= ID_NUM; j++)
		{
			sprintf(fifo[i][j], ".fifo%02d%02d", i, j);
		}
}

void shm_interface_init()
{
	id_list = (int*)shm;
	for(int i = 0; i <= ID_NUM; i++)
	{
		pid[i] = (int*)(shm + INFO_BASE + i * INFO_LEN);
		name[i] = (char*)pid[i] + PID_LEN;
		ip[i] = name[i] + NAME_LEN;
		mes[i] = shm + MES_BASE + i * MES_LEN;
	}
}
int have_id(int x)
{
	return (*id_list & (1 << x)) > 0;
}

int get_id()
{
	for(int i = 1; i <= ID_NUM; i++)
		if(!have_id(i))
		{
			*id_list += (1 << i);
			return i;
		}
	return -1;
}

int put_id(int id)
{
	if(have_id(id))
		*id_list -= 1 << id;
}

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


int left(int id)
{
	for(int i = 1; i <= ID_NUM; i++)
	{
		try_remove(fifo[id][i]);
		try_remove(fifo[i][id]);
	}
	sprintf(tmp, "*** User '%s' left. ***", name[id]);
	bc(tmp);
	put_id(id);
	return -1;	
}

void set_idenv(int id, char a[], char b[])
{
	int i;
	for(i = 0; env[id][i] != 0; i++)
		if(is_cmd(a, env[id][i]))
		{
			sprintf(env[id][i], "%s=%s", a, b);
			return;
		}
	env[id][i] = new char[100];
	sprintf(env[id][i], "%s=%s", a, b);
	return;
}

extern char ** environ;
int do_env(int id, char* argv[])
{
	if(strcmp("exit", argv[0]) == 0)
		return left(id);
	if(strcmp("setenv", argv[0]) == 0)
	{
		if(argv[1] == 0)
			for(int i = 0; environ[i] != 0; i++)
				printf("%s\n", environ[i]);
		else
			set_idenv(id, argv[1], argv[2]);
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

int is_cmd(char pat[], char t[])
{
	for(int i = 0; pat[i] != 0 && t[i] != 0; i++)
		if(pat[i] != t[i])
			return false;
	return true;
}

void bc(char s[])
{
	for(int i = 1; i <= ID_NUM; i++)
		if(have_id(i))
			writen(id_to_fd[i], s, strlen(s));
}

void msg(int id, char s[])
{
	writen(id_to_fd[id], s, strlen(s));
}



int do_chat(int id, char* line)
{
	if(is_cmd("yell ", line))
	{
		line += 5;
		while(*line == ' ') line++;
		sprintf(tmp, "*** %s yelled ***: %s\n", name[id], line);
		bc(tmp);
		return 1;
	}
	if(is_cmd("name ", line))
	{
		line += 5;
		while(*line == ' ') line++;
		for(int i = 1; i <= ID_NUM; i++)
		{
			if(i != id && have_id(i) && strcmp(name[i], line) == 0)
			{
				printf("*** User '%s' already exists. ***\n", line);
				fflush(stdout);
				return 1;
			}
		}
		strcpy(name[id], line);
		sprintf(tmp, "*** User from %s is named '%s'. ***\n", ip[id], name[id]);
		bc(tmp);
		return 1;
	}
	if(is_cmd("tell ", line))
	{
		line += 5;
		while(*line == ' ') line++;
		int sockd_tell = 0;
		while(*line <= '9' && *line >= '0')
			sockd_tell = sockd_tell * 10 + *(line++) - '0';
		if(!have_id(sockd_tell))
		{
			printf("*** Error: user #%d does not exist yet. ***\n", sockd_tell);
			fflush(stdout);
			return 1;
		}
		line += 1;
		sprintf(tmp, "*** %s told you ***: %s\n", name[id], line);
		msg(sockd_tell, tmp);
		return 1;
	}
	if(is_cmd("who", line))
	{	
		puts("<ID>\t<nickname>\t<IP/port>\t<indicate me>");
		for(int i = 1; i <= ID_NUM; i++)
		{
			if(have_id(i))
			{
				printf("%d\t%s\t%s", i, name[i], ip[i]);
				if(id == i)
					printf("\t<-me");
				puts("");
				fflush(stdout);
			}
		}
		return 1;
	}
	return 0;
}

int do_command(char* argv[], int in_fd, int out_fd, int err_fd = -1)
{
	int status = 0;
	int return_pid;
	int exec_pid = fork();
	if(exec_pid < 0)
		err_dump("exec: fork error");
	else if(exec_pid == 0)
	{
		int tmp = dup(STDERR_FILENO);
		if(out_fd != -1)
			dup2(out_fd, STDOUT_FILENO);
		if(in_fd != -1)
			dup2(in_fd, STDIN_FILENO);
		if(err_fd != -1)
			dup2(err_fd, STDERR_FILENO);
		execvp(argv[0], argv);
		dup2(tmp, STDERR_FILENO);
		fprintf(stderr, "Unknown command: [%s].\n", argv[0]);
		exit(1);
	}
	else
		wait(&status);
	//printf("%d %d %d\n",exec_pid, return_pid, status);
	fflush(stdout);
	fflush(stderr);
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

int try_remove(char * f)
{
	if(f == NULL)
		return 1;
	remove(f);
	return 0;
}

void receive(int from, char line[])
{
	sprintf(tmp, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n",
		name[cur_id], cur_id, name[from], from, line);
	bc(tmp);
}

#define next(x) x = (x+1)%1000


void exec_line(int id, char *argv[], int pfd[MAXLINE][2], int& pipe_cnt, char line[])
{
	int beg = 0;
	FILE* fin;
	int fin_no = pfd[pipe_cnt][1];
	int from = -1;
	char * removing_file = 0;
	// find any pipe and redirect
	for(int i = 0; argv[i] != NULL; i++)
	{
		if(argv[i][0] != '>' && argv[i][0] != '|' && argv[i][0] != '<')
			continue;
		FILE* fout;
		int pipe_num;

		if(argv[i][0] == '<')
		{
			int in = atoi(argv[i] + 1);
			fin = fopen(fifo[in][id], "r");
			if(fin == NULL)
			{
				printf("*** Error: the pipe #%d->#%d does not exist yet. ***\n", in, id);
				fflush(stdout);
				pipe_cnt += 1;
				pipe_cnt %= 1000;
				try_close(&pfd[pipe_cnt][0]);
				return;
			}
			fin_no = fileno(fin);
			removing_file = fifo[in][id];
			from = in;
			receive(in, line);
			argv[i] = 0;
			continue;
		}
		if(argv[i][0] == '>')
		{
			char* file;
			int out = -1;
			int err = -1;
			if(argv[i][1] != 0)
			{
				out = atoi(argv[i] + 1);
				if(!have_id(out))
				{
					printf("*** Error: user #%d does not exist yet. ***\n", out);
					fflush(stdout);
					next(pipe_cnt);
					try_close(&pfd[pipe_cnt][0]);
					return;
				}
				err = 1;
				file = fifo[id][out];
				if(access(file, F_OK) != -1)
				{
					printf("*** Error: the pipe #%d->#%d already exists. ***\n", id, out);
					fflush(stdout);
					next(pipe_cnt);
					try_close(&pfd[pipe_cnt][0]);
					return;
				}
				argv[i] = 0;
			}
			else
			{
				argv[i] = 0;
				file = argv[++i];
			}
			if(argv[i+1] != NULL && argv[i + 1][0] == '<')
			{
				i += 1;
				int in = atoi(argv[i] + 1);
				fin = fopen(fifo[in][id], "r");
				if(fin == NULL)
				{
					printf("*** Error: the pipe #%d->#%d does not exist yet. ***\n", in, id);
					fflush(stdout);
					next(pipe_cnt);
					try_close(&pfd[pipe_cnt][0]);
					return;
				}
				fin_no = fileno(fin);
				removing_file = fifo[in][id];
				from = in;
				argv[i] = 0;
				receive(in, line);
			}
			fout = fopen(file, "w");
			if(err)
				err = fileno(fout);
			if(fout == NULL)
			{
				debug("open file fail");
				return;
			}
			if(do_command(argv + beg, fin_no, fileno(fout), err) != 0)
			{
				if(out != -1)
					try_remove(file);
				return;
			}
			try_remove(removing_file);
			if(out != -1)
			{
				sprintf(tmp, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n",
					name[id], id, line, name[out], out);
				bc(tmp);
			}
			fclose(fout);
		}
		else if(argv[i][0] == '|')
		{
			pipe_num = pipe_cnt + (argv[i][1] == 0 ? 1 : atoi(argv[i] + 1));
			pipe_num = pipe_num > 1000 ? pipe_num - 1001 : pipe_num;
			if(pfd[pipe_num][0] == -1) // if pipe not open, open it.
				pipe(pfd[pipe_num]);
			argv[i] = 0;
			if(do_command(argv + beg, fin_no, pfd[pipe_num][0]) != 0)
				return;
			try_remove(removing_file);
			//printf("wat = %d\n", wat);
			//fflush(stdout);
		}
		try_close(&pfd[pipe_cnt][1]);
		beg = i + 1;
		next(pipe_cnt);
		try_close(&pfd[pipe_cnt][0]);
		fin_no = pfd[pipe_cnt][1];
	}
	if(argv[beg] != 0) // final command, if there is.
	{
		if(do_command(argv + beg, fin_no, -1) != 0)
		{
			if(beg == 0)
				next(pipe_cnt);
			try_close(&pfd[pipe_cnt][0]);
			return;
		}
		try_remove(removing_file);
		try_close(&pfd[pipe_cnt][1]);
		next(pipe_cnt);
		try_close(&pfd[pipe_cnt][0]);
	}
}


/*void shell_init()
{
	signal_init();
	sprintf(mes[0], "*** User '(no name)' entered from %s. ***", ip[id]);
	kill(0, SIGUSR1);	
}*/


int pipe_cnt[ID_NUM+1];
int pfd[ID_NUM+1][MAXLINE][2];

void shell_init(int fd)
{
	writen(fd,	
			"****************************************\n"
			"** Welcome to the information server. **\n"
			"****************************************\n",
			123);
	id[fd] = get_id();
	id_to_fd[id[fd]] = fd;
	//sprintf(ip[id[fd]],"%s/%d", inet_ntoa(cli_addr.sin_addr), (int)ntohs(cli_addr.sin_port));
	strcpy(ip[id[fd]], "CGILAB/511");
	strcpy(name[id[fd]], "(no name)");
	char s1[] = "PATH=bin:.";
	env[id[fd]][0] = new char[100];
	strcpy(env[id[fd]][0], s1);
	env[id[fd]][1] = 0;
	for(int i = 0; i < MAXLINE; i++)
		pfd[id[fd]][i][0] = pfd[id[fd]][i][1] = -1;
	sprintf(tmp, "*** User '%s' entered from %s. ***\n", name[id[fd]], ip[id[fd]]);
	bc(tmp);
	writen(fd, "% ", 2);
}

int shell_proc(int fd, int id)
{
	cur_id = id;
	environ = (char **)env[id];
	int n;
	char line[MAXLINE];
	char* argv[MAXLINE];
	n = readline(fd, line, MAXLINE);
	if (n == 0) return 1; /* connection terminated */ 
	else if (n < 0) 	
		err_dump("socket_shell: readline error");
	
	if(line[n-2] == '\r')
		line[n-2] = 0;
	else
		line[n-1] = 0;
	char _line[MAXLINE];
	strcpy(_line, line);
	if(security(line))
	{
		fputs("illegal character: '/'.\n", stderr);
		return 0;
	}
	if(do_chat(id, line)) // yell
		return 0;
	int argc = 0;
	char delimiter[] = {' ', '\n', 13};
	argv[argc] = strtok(line, delimiter);
	while(argv[argc] != 0)
		argv[++argc] = strtok(NULL, delimiter);
	/*for(int j = 0; argv[j] != 0; j++)
	for(int i = 0; argv[j][i] != 0; i++)
		printf("%d ", argv[j][i]);
	debug("");*/
	if(argv[0] == 0)
		return 0;
	int tmp = do_env(id, argv);
	if(tmp == -1)
		return 1;
	if(tmp > 0) // printenv, setenv, exit
		return 0;
	exec_line(id, argv, pfd[id], pipe_cnt[id], _line);
	return 0;
}


int main(int argc, char *argv[])
{ 
	chdir("/net/cs/104/0416092/ras_sp");
	struct sockaddr_in fsin; /* the from address of a client*/
	struct sockaddr_in serv_addr;
	int msock; /* master server socket */
	fd_set rfds; /* read file descriptor set */
	fd_set afds; /* active file descriptor set */
	int alen; /* from-address length */
	int fd, nfds;
	char* pname = argv[0];
	if((msock=socket(AF_INET, SOCK_STREAM, 0)) < 0)
		err_dump("server: can't open socket");
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(SERV_TCP_PORT);
	if(bind(msock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
		err_dump("server: can't bind local address");
	if(listen(msock, 30) < 0)
		err_dump("listen");

	nfds = 1024;
	FD_ZERO(&afds);
	FD_SET(msock, &afds);
	
	shm_interface_init();
	*id_list = 0;
	fifo_init();
	while (1) {
		memcpy(&rfds, &afds, sizeof(rfds));
		if (select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0)
			err_dump("select");
		if (FD_ISSET(msock, &rfds)) 
		{
			int ssock;
			alen = sizeof(fsin);
			ssock = accept(msock, (struct sockaddr *)&fsin, (unsigned *)&alen);
			if (ssock < 0)
				err_dump("accept");
			FD_SET(ssock, &afds);
			shell_init(ssock);
		}
		for (fd=0; fd<nfds; ++fd)
			if (fd != msock && FD_ISSET(fd, &rfds))
			{
				int _stdout = dup(1);
				int _stderr = dup(2);
				dup2(fd, 1);
				dup2(fd, 2);
				int exiting = shell_proc(fd, id[fd]);
				if (!exiting)
					writen(fd, "% ", 2);
				dup2(_stdout, 1);
				dup2(_stderr, 2);

				if (exiting) 
				{
					(void) close(fd);
					FD_CLR(fd, &afds);
				}
			}
	}
}


/*int main(int argc, char* argv[])
{
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

	listen(sockfd, 30);
	for ( ; ; ) {
		clilen = sizeof(cli_addr);
		newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
		
		if (newsockfd < 0) err_dump("server: accept error");
		if ( (childpid = fork()) < 0) err_dump("server: fork error");
		else if (childpid == 0) // child process 
		{
			// close original socket
			close(sockfd);
			// process the reques
			shm = (char *)shmat(shm_id, NULL, 0);
			shm_interface_init();
			get_id();
			//sprintf(ip[id],"%s/%d", inet_ntoa(cli_addr.sin_addr), (int)ntohs(cli_addr.sin_port));
			strcpy(ip[id], "CGILAB/511");
			strcpy(name[id], "(no name)");
			*pid[id] = getpid();
			
			socket_shell(newsockfd);
			exit(0);
		}
		close(newsockfd); // parent process 
	}
	shmctl(shm_id, IPC_RMID, NULL);
}*/
