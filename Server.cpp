/* A simple server in the internet domain using TCP
   The port number is passed as an argument */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

void error(const char *msg)
{
  perror(msg);
  exit(1);
}

void child_echo() {
  size_t const BUFF_SIZE = 8;
  int const STDOUT_FD = 1;
  char buf;
  size_t bytes_read = 0;
  while ((bytes_read = read(STDIN_FILENO, &buf, 1)) > 0) {
    write(STDOUT_FD, &buf, bytes_read);
  }
  //close(read_fd); // close the read-end of the pipe
}

int main(int argc, char *argv[])
{
  int sockfd, newsockfd, portno;
  int error_code = 0;
  socklen_t clilen;
  char buffer[256];
  struct sockaddr_in serv_addr,
    cli_addr; // size of the address of the client
  int n;      // how many bytes were read on a read() call
  if (argc < 2) {
    fprintf(stderr,"ERROR, no port provided\n");
    exit(1);
  }

  sockfd = socket(AF_INET, SOCK_STREAM, 0); // internet domain, stream: TCP, OS flag: keep 0
  if (sockfd < 0)
    error("ERROR opening socket");

  // set up the server address structure
  bzero((char *) &serv_addr, sizeof(serv_addr));
  portno = atoi(argv[1]);
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY; // sets the address to the address of the host
  serv_addr.sin_port = htons(portno);     // converts the port number to the network byte order
  // tells the system which address/port to bind to
  if ((error_code = bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr))) < 0) {
    printf("Error code was %d\n", error_code);
    error("ERROR on binding");
  } else {
    printf("listening on %d:%d...\n", serv_addr.sin_addr.s_addr, serv_addr.sin_port);
  }

  int const MAX_WAITERS = 5; // maximum number of processes that can wait on a socket
  listen(sockfd, MAX_WAITERS);
  clilen = sizeof(cli_addr);

  int pipefd[2];
  pipe(pipefd);   // creates a pipe
  pid_t pid = fork();
  if (pid == 0) {
    // child process
    close(STDIN_FILENO);
    close(pipefd[1]); // close the write-end of the pipe
    dup(pipefd[0]); // stdin redirected to the pipe
    close(pipefd[0]); // close the read-end of the pipe (in the dup case)
    child_echo();
    printf("child process exited\n");
    exit(0);
  } else if (pid < 0) {
    // fork failed
    // TODO we should clean up here
    error("FORK failed");
  }

  close(pipefd[0]); // closes read end of pipe

  // now we block until a client connects to the server
  do {
    newsockfd = accept(sockfd,
                       (struct sockaddr *) &cli_addr,
                       &clilen);
    if (newsockfd < 0)
      error("ERROR on accept");
    bzero(buffer, 256);
    n = read(newsockfd, buffer, 255);
    if (n < 0)
      error("ERROR reading from socket");
    printf("Sending message to child...\n");
    write(pipefd[1], buffer, n); // write to the pipe which the forked process reads from
    n = write(newsockfd, "I got your message", 18);
    if (n < 0)
      error("ERROR writing to socket");
    close(newsockfd);

  } while (buffer[0] != 'q');
  close(pipefd[1]); // closes write end of the pipe, sends an EOF to the child which will terminate it
  close(sockfd);
  wait(NULL); // wait for child process to exit
  return 0;
}