#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <syslog.h>

#define PORT	"9000"
#define BACKLOG 10

#define BUFFER_SIZE 1024

static volatile sig_atomic_t exit_requested = 0;

static void signal_handler(int sig)
{
	(void)sig;
    int saved_errno = errno;
	exit_requested = 1;
    errno = saved_errno;
    syslog(LOG_INFO, "Caught signal, exiting\n");
	return;
}


static int setup_server(const char *port)
{
	// get server address info
	struct addrinfo hints, *server_info;
	memset(&hints, 0, sizeof(hints));

	// populate hints with what we know
	hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use host IP

	// NULL -> fill in IP for me
	int err = getaddrinfo(NULL, port, &hints, &server_info);
	if (err) {
		perror("Failed to get serverinfo");
		return -1;
	}

	struct addrinfo *p;
	int server_fd;
    int yes = 1;
	// bind to first result that we can
	for (p = server_info; p != NULL; p = p->ai_next) {
		server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (server_fd == -1) {
			perror("Call to socket failed");
			continue; // try next result
		}

        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

		if (bind(server_fd, p->ai_addr, p->ai_addrlen) == -1) {
			close(server_fd);
			perror("Call to bind failed");
			continue; // try next result
		}

		// call to socket and bind worked
		break;
	}

	// server_info not used after this
	freeaddrinfo(server_info);

	if (p == NULL) {
		perror("Failed to bind to socket");
		return -1;
	}

	return server_fd;
}

static void send_client(const int client_fd, const char *file_path)
{
	const int fd = open(file_path, O_RDONLY, S_IRUSR | S_IRGRP | S_IROTH);
	if (fd == -1) {
		perror("Failed to send client");
		return;
	}

	char buffer[BUFFER_SIZE] = {0};
	int bytes_read = 0;
	// read chunks from file and send to client until entire file is read
	while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
		const int bytes_sent = send(client_fd, buffer, bytes_read, 0);
		if (bytes_sent != bytes_read) {
			syslog(LOG_ERR,
				"send_client: bytes read from file != bytes sent to client");
			// todo: handle
		}
	}

	close(fd);
	return;
}

void *get_in_addr(const struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in *)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

#define MAX_RECV_SIZE 10

static int handle_connection(const int client_fd, const struct sockaddr_storage *client_addr,
			     const char *file)
{
	char ip[INET6_ADDRSTRLEN] = {0};

	inet_ntop(client_addr->ss_family, get_in_addr((struct sockaddr *)client_addr), ip,
		  sizeof(ip));

	syslog(LOG_INFO, "Accepted connection from %s\n", ip);

	const int fd = open(file, O_CREAT | O_APPEND | O_RDWR, 0644);
	if (fd == -1) {
		perror("open tmp file");
		return -1;
	}

	char *buffer = calloc(MAX_RECV_SIZE, sizeof(char));

	int bytes = 0;
	int bytes_read = 0;

	while ((bytes = recv(client_fd, buffer + bytes_read, MAX_RECV_SIZE, 0)) > 0) {
		bytes_read += bytes;

		char *end = memchr(buffer, '\n', bytes_read);
		if (end != NULL) {
			// process bytes until new line
			const int str_len = end - buffer + 1;
			char *str = malloc(str_len);  // extra one for '\0'
			memset(str, 0, str_len);
			strncpy(str, buffer, str_len - 1); // ignore new line when printing and leave it for NULL
			syslog(LOG_INFO, "received message: %s\n", str);

            if (write(fd, buffer, str_len) == -1) {
				perror("write");
				break;
			}
			free(str);
			str = NULL;

			send_client(client_fd, file);

			// move the remaining bytes to the front and realloc array
			bytes_read = bytes_read - str_len; // data after '\n'
			memmove(buffer, end + 1, bytes_read);
			buffer = reallocarray(buffer, bytes_read, sizeof(char));
		}

		buffer = reallocarray(buffer, bytes_read + MAX_RECV_SIZE, sizeof(char));
	}

	free(buffer);
	buffer = NULL;

    syslog(LOG_INFO, "Closed connection from %s\n", ip);

	close(fd);
    close(client_fd);
	return 0;
}

int main(int argc, char **argv)
{
    bool daemon_mode = false;
    if ((argc == 2) && (strncmp(argv[1], "-d", strlen("-d"))) == 0) {
        daemon_mode = true;
        syslog(LOG_INFO, "running in daemon mode\n");
    }

	openlog("aesdsocket", 0, LOG_USER);

	const int server_fd = setup_server(PORT);
	if (server_fd == -1) {
		syslog(LOG_ERR, "setup server failed\n");
		return 1;
	}

	if (listen(server_fd, BACKLOG) == -1) {
		perror("Call to listen failed");
		close(server_fd);
		return 1;
	}

    if (daemon_mode)
    {
        pid_t pid = fork();

        if (pid > 0)
            exit(0);

        setsid();

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

	syslog(LOG_INFO, "server: waiting for connections...\n");

	struct sigaction sig_action;
	memset(&sig_action, 0, sizeof(sig_action));

	sig_action.sa_handler = signal_handler;

	sigaction(SIGINT, &sig_action, NULL);
	sigaction(SIGTERM, &sig_action, NULL);

	const char *file = "/var/tmp/aesdsocket";

	struct sockaddr_storage client_addr;
	socklen_t client_addr_len;

	while (!exit_requested) {
		client_addr_len = sizeof(client_addr);

		const int client_fd =
			accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client_fd == -1) {
			if (errno == EINTR) {
				break;
			}
			perror("accept");
			continue;
		}
		
        handle_connection(client_fd, &client_addr, file);
	}

	close(server_fd);
	remove(file);
	closelog();

	return 0;
}

