#include<err.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<unistd.h>
#include <pthread.h>
#include <stdbool.h>

#define DEFAULT_THR_NUM 4
typedef struct circ_buffer_t CircularBuffer;
pthread_mutex_t hc_mut;
pthread_mutex_t th_mut;
pthread_cond_t hc_cond;
pthread_cond_t th_cond;

struct Node{
  int serv_port;
  int num_req;
  int errs;
  int priority;
  int entries;
  int status;
  int succ_rate;
  
  char *serv1_port;
  // pthread_mutex_t mut;
  // pthread_cond_t
  struct Node *next;
};

typedef struct threadarg_t{
  // int client_sockfd;
  CircularBuffer *pcb;
  struct Node *curr_node;
  int client_connection_fd;
  int totalReqs;
  int sig;
  //char *list_port;
  //char *logname;
}ThreadArg;

struct Node *head = NULL;
//struct Node *curr = NULL;

//created using c linked list example from tutorialspoint
void insert(char *server_port){
  struct Node *link = (struct Node*) malloc(sizeof(struct Node));

  //link->key = key;
  link->serv_port = atoi(server_port);
  link->num_req = 0;
  link->next = head;
  head = link;
}

typedef struct circ_buffer_t{
  int *pInt_queue;
  int *client;
  ssize_t pq_size;
  ssize_t max_size;
  ssize_t lock_var;
  int head;
  int tail;
  pthread_mutex_t mut;
  pthread_cond_t discond;
  pthread_cond_t workercond;
  //pthread_cond_t thr_cond;
}CircularBuffer;

void CBuff_SeqEnqueue(CircularBuffer *pcb, int client_socket){
   //if queue is full, wait for dequeue signal use pthread_cond_wait
  while(pcb->pq_size == pcb->max_size){
    pthread_mutex_unlock(&pcb->mut);
    usleep(20);
    pthread_mutex_lock(&pcb->mut);
  } 

  pcb->pInt_queue[pcb->head] = client_socket;
  pcb->pq_size += 1;
  //printf("item enqueued...\n");

  if((pcb->pq_size == 1) && (pcb->pInt_queue[pcb->tail] == 0)){

    pcb->tail += 1;
  }
  else{
    while(pcb->pInt_queue[pcb->tail] == 0 && pcb->head != pcb->tail){
      pcb->tail =+ 1;
    }
  }
   
  if(pcb->head == pcb->max_size){
    pcb->head = pcb->pInt_queue[0];
  }
  else{
    pcb->head += 1;
  }
  //printf("head...%d\ntail...%d\n", pcb->pInt_queue[pcb->head], pcb->pInt_queue[pcb->tail]);
  return;
}

/*
   Dequeue for the circular buffer used to store client sockets awaiting a thread to run their request.
   Lock the thread, check for an empty queue, if empty, wait for a signal from enqueue. Othwerwise, remove 
   client socket from queue and increment the tail pointer accordingly. Send a condition variable signal 
   to alert the enqueue function it can enqueue, if it is waiting. The signal is also listened for in the
   thread func, once it receives this signal, the thread starts running the functionality. 
*/
int CBuff_SeqDequeue(CircularBuffer *pcb){
  while(pcb->pq_size == 0){
    pthread_mutex_unlock(&pcb->mut);
    usleep(20);
    pthread_mutex_lock(&pcb->mut);
  }
   
   /*
      Set value of element pointed at by head to the client socket accepted in main.
      Increment size of CB.
   */
   int value = pcb->pInt_queue[pcb->tail];
   pcb->pInt_queue[pcb->tail] = 0;
   pcb->pq_size -= 1;
   //printf("item dequeued...\nsize: %ld\n", pcb->pq_size);
   //if head tail has reached end, reset to beginning. Otherwise, increment tail pointer
  if(pcb->tail == pcb->max_size){
    pcb->tail = pcb->pInt_queue[0];
  }
  else{
    pcb->tail += 1;
  }
   //printf("head...%d\ntail...%d\n", pcb->pInt_queue[pcb->head], pcb->pInt_queue[pcb->tail]);
   return value;
}
/*
 * client_connect takes a port number and establishes a connection as a client.
 * connectport: port number of server to connect to
 * returns: valid socket if successful, -1 otherwise
 */
int client_connect(uint16_t connectport) {
  int connfd;
  struct sockaddr_in servaddr;

  connfd=socket(AF_INET,SOCK_STREAM,0);
  if (connfd < 0)
    return -1;
  memset(&servaddr, 0, sizeof servaddr);

  servaddr.sin_family=AF_INET;
  servaddr.sin_port=htons(connectport);

  /* For this assignment the IP address can be fixed */
  inet_pton(AF_INET,"127.0.0.1",&(servaddr.sin_addr));

  if(connect(connfd,(struct sockaddr *)&servaddr,sizeof(servaddr)) < 0)
    return -1;
  return connfd;
}

/*
 * server_listen takes a port number and creates a socket to listen on 
 * that port.
 * port: the port number to receive connections
 * returns: valid socket if successful, -1 otherwise
 */
int server_listen(int port) {
  int listenfd;
  int enable = 1;
  struct sockaddr_in servaddr;

  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0)
    return -1;
  memset(&servaddr, 0, sizeof servaddr);
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htons(INADDR_ANY);
  servaddr.sin_port = htons(port);

  if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
    return -1;
  if (bind(listenfd, (struct sockaddr*) &servaddr, sizeof servaddr) < 0)
    return -1;
  if (listen(listenfd, 500) < 0)
    return -1;
  return listenfd;
}

/*
 * bridge_connections send up to 100 bytes from fromfd to tofd
 * fromfd, tofd: valid sockets
 * returns: number of bytes sent, 0 if connection closed, -1 on error
 */
int bridge_connections(int fromfd, int tofd) {
  char recvline[4000];
  memset(recvline, 0, strlen(recvline));
  int n = recv(fromfd, recvline, 4000, 0);
  if (n < 0) {
    //printf("connection error receiving\n");
    return -1;
  } else if (n == 0) {
    //printf("receiving connection ended\n");
    return 0;
  }
  recvline[n] = '\0';
  // printf(" data is: %s\n", recvline);
  //sleep(1);
  n = send(tofd, recvline, n, 0);
  //printf("bytes sent: %d\n", n);
  if (n < 0) {
    //printf("connection error sending\n");
    return -1;
  }else if (n == 0) {
    //printf("sending connection ended\n");
    return 0;
  }
  return n;
}

/*
 * bridge_loop forwards all messages between both sockets until the connection
 * is interrupted. It also prints a message if both channels are idle.
 * sockfd1, sockfd2: valid sockets
 */
void bridge_loop(int sockfd1, int sockfd2) {
  fd_set set;
  struct timeval timeout;
  int fromfd, tofd;
  while(1) {

      // set for select usage must be initialized before each select call
      // set manages which file descriptors are being watched
    FD_ZERO (&set);
    FD_SET (sockfd1, &set);
    FD_SET (sockfd2, &set);

    // same for timeout
    // max time waiting, 5 seconds, 0 microseconds
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    // select return the number of file descriptors ready for reading in set
    int sel = select(FD_SETSIZE, &set, NULL, NULL, &timeout);
    switch (sel) {
      case -1:
        //printf("error during select, exiting\n");
        dprintf(sockfd1, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
        //send sockfd1, 500 msg

        //close client on LB listen
        return;
      case 0:
        //printf("both channels are idle, waiting again\n");
        dprintf(sockfd1, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
        continue;
      default:
        if (FD_ISSET(sockfd1, &set)) {
          fromfd = sockfd1;
          tofd = sockfd2;

        }else if (FD_ISSET(sockfd2, &set)) {
          fromfd = sockfd2;
          tofd = sockfd1;

        }else {
          //printf("this should be unreachable\n");
          return;
        }
    }

    int ret = bridge_connections(fromfd, tofd);

    if(ret <= 0){
      return;
    }
    else{
      continue;
    }
  }
}

void hc_func(struct Node *nd){
  struct Node *ptr;
  struct Node *min = nd;
  struct Node *list = nd;
  int connfd;
  uint16_t connport;
  char hc_buf[500];
  int stat_code, con_len;

  // struct timespec waitTime;
  // waitTime.tv_sec = 3;

  //lock thread function
  // pthread_mutex_lock(&hc_mut);
  // pthread_cond_timedwait(&hc_cond,&hc_mut, &waitTime);
  printf("in hc func\n");
  for(ptr = list; ptr != NULL; ptr = ptr->next){

    connport = ptr->serv_port;

    if ((connfd = client_connect(connport)) < 0){
      err(1, "failed connecting: %d", connfd);
    }
    dprintf(connfd, "GET /healthcheck HTTP/1.1\r\n\r\n");
    //sleep(1);
    int r = recv(connfd, hc_buf, 500, 0);
    hc_buf[r] = '\0';
    printf("hc buffer: %s\n", hc_buf);
    if(r<0){
      ptr->status = 0;
      continue;
    }
    else{
      // if(strcmp(recvline, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n") == 0){

      // }
      sscanf(hc_buf, "HTTP/1.1 %d OK\r\nContent-Length: %d\r\n\r\n", &stat_code, &con_len);
      
      if(stat_code == 200){
        r = recv(connfd, hc_buf, con_len, 0);
        hc_buf[r] = '\0';
        printf("hc buffer: %s\nbuf len: %ld\n", hc_buf, strlen(hc_buf));
        sscanf(hc_buf, "%d\n%d", &ptr->errs, &ptr->entries);
        printf("ptr->ents:%d\n", ptr->entries);
        if(ptr->entries == 0){
          ptr->succ_rate = 0;
        }
        else{
          ptr->succ_rate = 1-((ptr->errs)/(ptr->entries));
        }
        ptr->status = 1;

        if(ptr->entries < min->entries){
          min = ptr;
        }
        
      }
      else{
        ptr->status = 0;
        ptr->priority = 0;
      }

    }
    //printf("here end\n");
  }//end for

  for(ptr = list; ptr != NULL; ptr = ptr->next){
    if(ptr->status == 1){
      if(min->entries == ptr->entries){
        if(min->succ_rate >= ptr->succ_rate){
          continue;
        }
        else{
          min = ptr;
        }
      }
    }
    else{
      continue;
    }
  }
  min->priority = 1;
  // pthread_mutex_unlock(&hc_mut);
  // pthread_cond_signal(&hc_cond);
}

//parameter must be of type Node*
void* HC_thread(void *object){
  ThreadArg *parg = (ThreadArg*) object;

  while(true){
    if(((parg->totalReqs % parg->sig) == 0) && (parg->totalReqs != 0)){
      pthread_mutex_lock(&hc_mut);
      pthread_cond_wait(&hc_cond, &hc_mut);
      hc_func(parg->curr_node);
      pthread_mutex_unlock(&hc_mut);
      pthread_cond_signal(&hc_cond);
    }
  }

return NULL;
}

/*
  This thread function
*/
void* thread_func(void *obj){
  ThreadArg *parg = (ThreadArg*) obj;
  CircularBuffer *pcb = parg->pcb;
  int connfd;
  uint16_t connectport;
  struct Node *current;
  while(true){
    
    //we must lock the thread in order to call wait()
    pthread_mutex_lock(&pcb->mut);
    /*
      We will call wait on each thread if the queue is empty, and then wait for a signal from enqueue to continue.
        
    */
    while(pcb->pq_size == 0){
      pthread_cond_wait(&pcb->workercond, &pcb->mut); 
    }
    /*
       Once we continue, this thread has acquired the lock in order to dequeue an element.
       We then dequeue, unlock the lock, and send a signal that the circ_buffer is free.
    */

    int client_sockd1 = CBuff_SeqDequeue(pcb);
     
    pthread_mutex_unlock(&pcb->mut);
    pthread_cond_signal(&pcb->discond);

    for(current = parg->curr_node; current != NULL; current = current->next){
      if(current->priority == 1){
        if(current->status == 1){
          break;
        }
        else{
          continue;
        }
      }
    }
    //if all servers are down, return 500 err
    if(current->next == NULL && current->priority != 1){
      dprintf(client_sockd1, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
      continue;
    }

    connectport = current->serv_port;
    if ((connfd = client_connect(connectport)) < 0)
      err(1, "failed connecting: %d", connfd);
    

    bridge_loop(client_sockd1, connfd);
    parg->totalReqs += 1;
    close(client_sockd1);
    close(connfd);

    continue;
    
  }
   return NULL;
}

int main(int argc,char **argv){
  int listenfd, opt_ret, req_sig;
  int num_thr = DEFAULT_THR_NUM;
  uint16_t listenport;
  char *req_HC = "";
  char *LB_listen = "";
  //char *serv_listen;
  char *thrNum = "";
  int portCheck;

  if (argc < 3) {
    printf("missing arguments: usage %s port_to_connect port_to_listen\n", argv[0]);
    return 1;
  }
  while((opt_ret = getopt(argc, argv, "N:R:")) != -1){
    switch(opt_ret){
      case 'R':
        req_HC = optarg;
        continue;
      case 'N':
        thrNum = optarg;
        continue;
      case '?':
        return -1;
      default:
        continue;
    }
  }
  if(thrNum != NULL){
    sscanf(thrNum, "%d", &num_thr);
  }
  if(req_HC != NULL){
    sscanf(req_HC, "%d", &req_sig);
  }
  
  if(argv[optind] != NULL){
    ssize_t ctr = 0;
    //printf("optind = %s\n", argv[optind]);
    
    //loop through rest of argv, first arg is LB_listen, rest are server ports
    for(ssize_t idx = optind; idx < argc; ++idx){
      sscanf(argv[idx], "%d", &portCheck);
      if(portCheck <= 0){
        return -1;
      }
      if(ctr == 0){
        LB_listen = argv[idx];
        ++ctr;
      }
      else{
        //sscanf(argv[idx], "%d", &portNum);
        insert(argv[idx]);
        ++ctr;
      }
    }    
  }
  else{
    return -1;
  }


  listenport = atoi(LB_listen);

  if ((listenfd = server_listen(listenport)) < 0)
    err(1, "failed listening");

  CircularBuffer *cb = malloc(sizeof(CircularBuffer));
  cb->pInt_queue = malloc(sizeof(int) * num_thr);
  cb->head = 0;
  cb->tail = 0;
  cb->max_size = 5;
  int cond_init = pthread_cond_init(&cb->discond, NULL);
  int cond_init1 = pthread_cond_init(&cb->workercond, NULL);
  if(cond_init < 0 || cond_init1 < 0){
    return -1;
  }
  pthread_mutex_init(&cb->mut, NULL);
  pthread_mutex_init(&hc_mut, NULL);
  pthread_t thread_ids[num_thr];
  //pthread_t HC_thr;
  ThreadArg args;
  args.pcb = cb;
  args.curr_node = head;
  args.totalReqs = 0;
  args.sig = req_sig;

  // Remember to validate return values
  // You can fail tests for not validating
   


  if(args.totalReqs == 0){
    hc_func(head);
  }
  

  for(ssize_t idx = 0; idx < num_thr; ++idx){
    pthread_create(&thread_ids[idx], NULL, thread_func, &args);
  }
  //pthread_create(&HC_thr, NULL, HC_thread, &args);
 while(1){
    //if queue full, pthread_cond_wait

    while(cb->pq_size >= cb->max_size){
       pthread_cond_wait(&cb->discond, &cb->mut);
    }

    int client_sockd = accept(listenfd, NULL, NULL);
    //printf("csok: %d\n", client_sockd);
    pthread_mutex_lock(&cb->mut);
    CBuff_SeqEnqueue(cb, client_sockd);

    pthread_mutex_unlock(&cb->mut);
    pthread_cond_signal(&cb->workercond);
    
 }

//separate thread for HC
  return 0;
}