#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

/*
 * Serves the contents the file stored at `path` to the client socket `fd`.
 * It is the caller's reponsibility to ensure that the file stored at `path` exists.
 * You can change these functions to anything you want.
 * 
 * ATTENTION: Be careful to optimize your code. Judge is
 *            sesnsitive to time-out errors.
 */
void serve_file(int fd, char *path) {
    int file_fd = open(path, O_RDONLY);
    if (file_fd < 0) {
        http_start_response(fd, 404);
        http_send_header(fd, "Content-Type", "text/html");
        http_end_headers(fd);
    } else {
        struct stat statbuf;
        if (fstat(file_fd, &statbuf) < 0) {
            // Handle error if fstat fails
            close(file_fd);
            return;
        }

        long length = statbuf.st_size;
        char content_length[20];
        snprintf(content_length, sizeof(content_length), "%ld", length);

        http_start_response(fd, 200);
        http_send_header(fd, "Content-Type", http_get_mime_type(path));
        http_send_header(fd, "Content-Length", content_length);
        http_end_headers(fd);

        void *file_memory = mmap(NULL, length, PROT_READ, MAP_PRIVATE, file_fd, 0);
        if (file_memory == MAP_FAILED) {
            // mmap failed, handle error
            close(file_fd);
            return;
        }

        http_send_data(fd, (const char *)file_memory, length);
        munmap(file_memory, length);
        close(file_fd);
    }
    close(fd);
}

void serve_directory(int fd, char *path) {
    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/index.html", path);
    FILE *index_file = fopen(index_path, "r");
    if (index_file) {
        fclose(index_file);
        serve_file(fd, index_path);
        return;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        http_start_response(fd, 404);
        http_send_header(fd, "Content-Type", "text/html");
        http_end_headers(fd);
    } else {
        http_start_response(fd, 200);
        http_send_header(fd, "Content-Type", "text/html");
        http_end_headers(fd);

        const char *header = "<html><body><h2>Directory listing for ";
        const char *footer = "</h2></body></html>";
        char *body = malloc(4096); // Initial buffer size
        strcpy(body, header);
        strcat(body, path);
        strcat(body, "</h2><ul>");

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            char *entryLink = malloc(strlen(entry->d_name) + 32); // Allocate space for the link
            snprintf(entryLink, strlen(entry->d_name) + 32, "<li><a href='./%s'>%s</a></li>\n", entry->d_name, entry->d_name);
            if (strlen(body) + strlen(entryLink) + strlen(footer) >= 4096) {
                // Increase buffer size
                body = realloc(body, strlen(body) + strlen(entryLink) + strlen(footer) + 1024);
            }
            strcat(body, entryLink);
            free(entryLink);
        }
        strcat(body, "</ul>");
        strcat(body, footer);

        http_send_string(fd, body);
        free(body);
        closedir(dir);
    }
    close(fd);
}

/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 * 
 *   Closes the client socket (fd) when finished.
 */
void handle_files_request(int fd) {
    // Parse the HTTP request
    struct http_request *request = http_request_parse(fd);
    if (!request || request->path[0] != '/') {
        http_start_response(fd, 400);
    } else if (strstr(request->path, "..")) {
        http_start_response(fd, 403);
    } else {
        char resolved_path[1024];
        snprintf(resolved_path, sizeof(resolved_path), "%s%s", server_files_directory, request->path);

        struct stat path_stat;
        if (stat(resolved_path, &path_stat) != 0) {
            http_start_response(fd, 404);
        } else {
            if (S_ISREG(path_stat.st_mode)) {
                serve_file(fd, resolved_path);
                goto cleanup; // Avoid closing fd twice
            } else if (S_ISDIR(path_stat.st_mode)) {
                serve_directory(fd, resolved_path);
                goto cleanup; // Avoid closing fd twice
            } else {
                http_start_response(fd, 404);
            }
        }
    }

    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);

cleanup:
    if (request) {
        printf("Request fulfilled\n");
        free(request);
    }
    close(fd);
}

typedef struct {
    int src;
    int dest;
    pthread_cond_t *cond;
    int *is_finished;
} proxy_struct;

void *proxy_thread(void *args) {
    proxy_struct *proxyStruct = (proxy_struct *)args;
    int srcFd = proxyStruct->src;
    int destFd = proxyStruct->dest;
    pthread_cond_t *signalCond = proxyStruct->cond;

    ssize_t bytesRead;
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));

    while (!(*proxyStruct->is_finished)) {
        bytesRead = read(srcFd, buffer, sizeof(buffer) - 1);
        if (bytesRead <= 0) {
            break; // Exit if read error or EOF
        }
        buffer[bytesRead] = '\0';
        printf("message: %s\n", buffer);
        write(destFd, buffer, bytesRead); // Use write for sending data
    }

    *proxyStruct->is_finished = 1;
    if (signalCond != NULL) {
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock(&mutex);
        pthread_cond_broadcast(signalCond);
        pthread_mutex_unlock(&mutex);
    }

    return NULL;
}

/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int fd) {

  /*
  * The code below does a DNS lookup of server_proxy_hostname and 
  * opens a connection to it. Please do not modify.
  */

  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;
  target_address.sin_port = htons(server_proxy_port);

  struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

  int target_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (target_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    close(fd);
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    close(target_fd);
    close(fd);
    exit(ENXIO);
  }

  char *dns_address = target_dns_entry->h_addr_list[0];

  memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
  int connection_status = connect(target_fd, (struct sockaddr*) &target_address,
      sizeof(target_address));

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    http_request_parse(fd);

    http_start_response(fd, 502);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd, "<center><h1>502 Bad Gateway</h1><hr></center>");
    close(target_fd);
    close(fd);
    return;

  }
}


typedef struct {
    void (*handler)(int socket);
} worker_args;

void worker_routine(void *parameters) {
    worker_args *workerParams = (worker_args *)parameters;
    for (;;) {
        int socketDescriptor = wq_pop(&work_queue);
        if (socketDescriptor >= 0) {
            workerParams->handler(socketDescriptor);
            close(socketDescriptor);
        } else {
            break;
        }
    }
    free(parameters);
}

void init_thread_pool(int threadCount, void (*handlerFunc)(int)) {
    pthread_t *threads = malloc(sizeof(pthread_t) * threadCount);
    if (!threads) return;

    for (int i = 0; i < threadCount; ++i) {
        worker_args *args = malloc(sizeof(worker_args));
        if (args) {
            *args = (worker_args){.handler = handlerFunc}; 
            if (pthread_create(&threads[i], NULL, (void *(*)(void *))worker_routine, args) != 0) {
                free(args);
            }
        }
    }

    for (int i = 0; i < threadCount; ++i) {
        pthread_join(threads[i], NULL);
    }
    free(threads); // Cleanup
}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  init_thread_pool(num_threads, request_handler);

  while (1) {
    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);

    wq_init(&work_queue);

    if (num_threads == 0) {
      request_handler(client_socket_number);
      close(client_socket_number);
    } else {
      wq_push(&work_queue, client_socket_number);
    }
  }

  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);
  signal(SIGPIPE, SIG_IGN);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
