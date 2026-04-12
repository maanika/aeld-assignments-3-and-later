#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "queue.h"

#define PORT "9000"
#define BACKLOG 10
#define MAX_RECV_SIZE 1024
#define BUFFER_SIZE 1024

// TODO: this is a temp workaround buildroot not setting the flag
#define USE_AESD_CHAR_DEVICE 1

#ifdef USE_AESD_CHAR_DEVICE
const char *file = "/dev/aesdchar";
#else
const char *file = "/var/tmp/aesdsocket";
#endif /* USE_AESD_CHAR_DEVICE */

static volatile sig_atomic_t exit_requested = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
  int client_fd;
  atomic_bool flag;
  pthread_t thread_id;
  struct sockaddr_storage client_addr;
} thread_args_t;

// SLIST
typedef struct slist_data_s slist_data_t;
struct slist_data_s {
  thread_args_t *thread_args;
  SLIST_ENTRY(slist_data_s) entries;
};

static void signal_handler(int sig) {
  (void)sig;
  int saved_errno = errno;
  exit_requested = 1;
  errno = saved_errno;
  syslog(LOG_INFO, "Caught signal, exiting\n");
  return;
}

static int setup_server(const char *port) {
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

static void send_client(const int client_fd, const char *file_path) {
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

void *get_in_addr(const struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }
  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

static void *handle_connection(void *args) {
  int client_fd = (((thread_args_t *)args)->client_fd);
  int *ret = malloc(sizeof(int));

  char ip[INET6_ADDRSTRLEN] = {0};
  struct sockaddr_storage client_addr = (((thread_args_t *)args)->client_addr);
  inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr *)&client_addr),
            ip, sizeof(ip));

  printf("Accepted connection from %s\n", ip);
  syslog(LOG_INFO, "Accepted connection from %s\n", ip);

  const int fd = open(file, O_CREAT | O_APPEND | O_RDWR, 0644);
  if (fd == -1) {
    perror("open tmp file");
    *ret = -1;
    pthread_exit(&ret);
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
      char *str = malloc(str_len); // extra one for '\0'
      memset(str, 0, str_len);
      strncpy(str, buffer,
              str_len -
                  1); // ignore new line when printing and leave it for NULL
      syslog(LOG_INFO, "received message: %s\n", str);

      pthread_mutex_lock(&mutex);
      *ret = write(fd, buffer, str_len);
      pthread_mutex_unlock(&mutex);

      if (*ret == -1) {
        perror("write");
        break;
      }

      free(str);
      str = NULL;

      pthread_mutex_lock(&mutex);
      send_client(client_fd, file);
      pthread_mutex_unlock(&mutex);

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
  printf("Closed connection from %s\n", ip);

  close(fd);
  close(client_fd);
  ((thread_args_t *)args)->flag = true;
  *ret = 0;
  pthread_exit((void *)ret);
}

#ifndef USE_AESD_CHAR_DEVICE
static bool timer_initialised = false;

static void timer_thread(union sigval sigval) {
  (void)sigval;

  char time_str[200] = {0};
  time_t t;
  struct tm *tmp;

  t = time(NULL);
  tmp = localtime(&t);
  if (tmp == NULL) {
    perror("localtime");
    return;
  }

  if (strftime(time_str, sizeof(time_str), "%a, %d %b %Y %T %z", tmp) == 0) {
    fprintf(stderr, "strftime returned 0\n");
    return;
  }

  printf("Time is %s\n", time_str);

  const int fd = open(file, O_CREAT | O_APPEND | O_RDWR, 0644);
  if (fd == -1) {
    perror("open tmp file");
    return;
  }

  pthread_mutex_lock(&mutex);
  {
    write(fd, "timestamp:", strlen("timestamp:"));
    write(fd, time_str, strlen(time_str));
    write(fd, "\n", 1);
  }
  pthread_mutex_unlock(&mutex);

  close(fd);

  return;
}

int timer_init(timer_t *return_timerid, int interval_secs) {
  if (return_timerid == NULL)
    return -1;

  if (timer_initialised)
    return 0;

  // create timer
  timer_t timerid;
  struct sigevent sev;
  memset(&sev, 0, sizeof(struct sigevent));
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_value.sival_ptr = &timerid;
  sev.sigev_notify_function = &timer_thread;
  if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) == -1) {
    perror("timer_init, timer_create");
    return -1;
  }

  printf("Timer created ID is %#jx\n", (uint64_t)timerid);

  // Start the timer
  struct itimerspec its;
  its.it_value.tv_sec = interval_secs;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = its.it_value.tv_sec;
  its.it_interval.tv_nsec = its.it_value.tv_nsec;
  if (timer_settime(timerid, 0, &its, NULL) == -1) {
    perror("timer_init, timer_settime");
    return -1;
  }

  timer_initialised = true;
  *return_timerid = timerid;
  return 0;
}

static void timer_deinit(timer_t timerid) {
  if (!timer_initialised)
    return;

  timer_delete(timerid);
  printf("Timer deleted ID is %#jx\n", (uint64_t)timerid);
}
#endif /* USE_AESD_CHAR_DEVICE */

int main(int argc, char **argv) {
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

  if (daemon_mode) {
    pid_t pid = fork();
    if (pid > 0) {
      exit(0);
    }

    setsid();

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
  }

#ifndef USE_AESD_CHAR_DEVICE
  timer_t timer_id;
  timer_init(&timer_id, 10);
#endif /* USE_AESD_CHAR_DEVICE */

  syslog(LOG_INFO, "server: waiting for connections...\n");

  struct sigaction sig_action;
  memset(&sig_action, 0, sizeof(sig_action));

  sig_action.sa_flags = 0;
  sig_action.sa_handler = signal_handler;

  sigaction(SIGINT, &sig_action, NULL);
  sigaction(SIGTERM, &sig_action, NULL);

  struct sockaddr_storage client_addr;
  socklen_t client_addr_len;

  slist_data_t *datap = NULL;
  SLIST_HEAD(slisthead, slist_data_s) head;
  SLIST_INIT(&head);

  void *return_value = NULL;
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

    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);

    thread_args_t *args = malloc(sizeof(thread_args_t));
    args->client_fd = client_fd;
    args->flag = false;
    args->thread_id = -1; // set to -1 for now, not used by thread, set to
                          // actual value after thread is created.
    args->client_addr = client_addr;

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, handle_connection, (void *)args);

    args->thread_id = thread_id;
    datap = malloc(sizeof(slist_data_t));
    datap->thread_args = args;
    SLIST_INSERT_HEAD(&head, datap, entries);

    slist_data_t *tmp;
    SLIST_FOREACH_SAFE(datap, &head, entries, tmp) {
      if (datap->thread_args->flag) {
        if (pthread_join(datap->thread_args->thread_id, &return_value) == 0) {
          free(return_value);
        }
        SLIST_REMOVE(&head, datap, slist_data_s, entries);

        // Free memory allocated for thread management
        free(datap->thread_args);
        free(datap);
      }
    }
  }

  // wait for all remaining threads to complete
  while (!SLIST_EMPTY(&head)) {
    datap = SLIST_FIRST(&head);
    if (datap->thread_args->flag) {
      if (pthread_join(datap->thread_args->thread_id, &return_value) == 0) {
        free(return_value);
      }
      SLIST_REMOVE_HEAD(&head, entries);
      free(datap->thread_args);
      free(datap);
    }
  }

#ifndef USE_AESD_CHAR_DEVICE
  timer_deinit(timer_id);
#endif /* USE_AESD_CHAR_DEVICE */

  close(server_fd);

#ifndef USE_AESD_CHAR_DEVICE
  remove(file);
#endif /* USE_AESD_CHAR_DEVICE */

  closelog();

  return 0;
}
