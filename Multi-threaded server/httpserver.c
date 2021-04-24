#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h> // write
#include <string.h> // memset
#include <stdlib.h> // atoi
#include <stdbool.h>
#include <math.h>
#include <pthread.h>

#define BUFFER_SIZE 512
#define STDOUT_FILENO 1
#define STDIN_FILENO 0
#define STDERR_FILENO 2
#define MAX_HEADER_SIZE 4096
#define READ_BUFF_LEN 1024
#define CIRC_BUF_LEN 2048
#define CODE_SUCCESS 200
#define CODE_CREATED 201
#define CODE_BAD_REQ 400
#define CODE_FORBID 403
#define CODE_FILE_NOT_FOUND 404
#define CODE_SERV_ERR 500
#define MAX_BUF_SIZE 16000
#define DEFAULT_THR 4

static const char termin[] = "\r\n\r\n";
const char *valid_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
int global_f_offset;
int log_fd;
typedef struct circ_buffer_t CircularBuffer;
pthread_mutex_t offset_mut = PTHREAD_MUTEX_INITIALIZER;
//pthread_cond_t off_cond;
int logging;
char msg_len[15];

struct httpObject {
    char method[5];         // PUT, HEAD, GET
    char fname_parse[28];      // what is the file we are worried about
    char httpversion[9];    // HTTP/1.1
    char con_len[15], fname[27];
    ssize_t content_len; // example: 13
    ssize_t data_len;
    ssize_t total_bytes_logged;
    ssize_t c_sock;
    int msg_fd;
    int status_code;
    char buffer[MAX_HEADER_SIZE];
    char log_buf[READ_BUFF_LEN];
};

typedef struct circ_buffer_t{
   int *pInt_queue;
   int *client;
   ssize_t pq_size;
   ssize_t max_size;
   ssize_t lock_var;
   int head;
   int tail;
   pthread_mutex_t mut;
   pthread_cond_t cond;
   //pthread_cond_t thr_cond;
} CircularBuffer;

typedef struct threadarg_t{
   // int client_sockfd;
   CircularBuffer *pcb;
   char *logname;
} ThreadArg;

void read_http_req(ssize_t client_sockd, struct httpObject* message){
   char *compare_str = "Content-Length";
   char *rest = NULL;
   char *token;
   int counter = 0;
   printf("%ld\n", client_sockd);

   for(token = strtok_r(message->buffer, "\r\n", &rest); token != NULL; token = strtok_r(NULL, "\r\n", &rest)){
      if(counter == 0){
         sscanf(token, "%s %s %s", message->method, message->fname_parse, message->httpversion); //parse first token
      }

      //if put, look for content-length in subsequent tokens
      if((strcmp(message->method, "GET") == 0) || (strcmp(message->method, "HEAD") == 0)){
         break;
      }
      else if(strcmp(message->method, "PUT") == 0){
         if((strstr(token, compare_str)) != 0){
            sscanf(token, "%s %ld", message->con_len, &message->content_len);
            break;
         }
         else{
            ++counter;
            continue;
         }
      }
   }

   memmove(message->fname, message->fname_parse+1,27);
   return;
}

/*
   This function will take in a FD for the log file if specified, a length, and a buffer or msg obj.
   The function will then print out the method, the fname preceeded by '/', and the content length
   of either the file or the data. It will then convert each character into hex and print out 20
   characters to line line, delimited by whitespace. A counter is needed to keep track of the
   amt. of chars written already as the beginning of each line will contain the amt of bytes/chars
   written to log. The last line is terminated by '========\n'.Before the func, obtain the offset 
   and increment based on length of data plus extra, all within a lock. After obtaining the lock, 
   pwrite the first string and then call this func appropriately.
   "Fail: er'" will also but put in the event of an error
*/

void log_req(int logFile_FD, int fileOffset, int openedFD, struct httpObject *msg){
   ssize_t in_buf_len = strlen(msg->log_buf);
   char log_buffer[20];
   char *one = "========\n";
   ssize_t bytes_logged = 0;
   ssize_t bytes_log_rd = in_buf_len;
   ssize_t read_log = in_buf_len;


   if(strlen(msg->log_buf) < READ_BUFF_LEN){
      for(ssize_t index = 0; index < in_buf_len; ++index){
         //place first bytes rd num and first hex char
         if((index == 0) && bytes_logged == 0){
            bytes_logged += snprintf(&log_buffer[0], 10, "%08ld ", index);
            fileOffset += pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
            //printf("foff: %d\n", fileOffset);
            bytes_logged += snprintf(&log_buffer[0], 4, "%02x ", msg->log_buf[index]);
            fileOffset += pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
            //printf("foff: %d\n", fileOffset);
         }
         //place subsequent bytes rd nums and following char
         else if(index % 20 == 0 && index != 0){
            bytes_logged += snprintf(&log_buffer[0], 10, "%08ld ", index);
            printf("%s\n", log_buffer);
            fileOffset += pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
            bytes_logged += snprintf(&log_buffer[0], 4, "%02x ", msg->log_buf[index]);
            fileOffset += pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
            //printf("foff: %d\n", fileOffset);
         }
         //if at last char before newline, place \n at EOL
         else if((index+1) % 20 == 0){
            bytes_logged += snprintf(&log_buffer[0], 4, "%02x\n", msg->log_buf[index]);
            fileOffset += pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
         }
         //add converted hex char to line
         else if(index == in_buf_len-1){
            bytes_logged += snprintf(&log_buffer[0], 4, "%02x\n", msg->log_buf[index]);
            fileOffset += pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
            //printf("foff: %d\n", fileOffset);
         }
         else{
            bytes_logged += snprintf(&log_buffer[0], 4, "%02x ", msg->log_buf[index]);
            int pr1 = pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
            //printf("pwrite: %d\n", pr1);
            fileOffset += pr1;
         }
      }
      fileOffset += pwrite(logFile_FD, one, strlen(one), fileOffset);

   }
   else{
      //loop read for large files
      ssize_t loopCt = 0;
      while(bytes_log_rd < msg->data_len && read_log > 0){
         for(ssize_t index = 0; index < in_buf_len; ++index){
            //place first bytes rd num and first hex char
            if((index == 0) && bytes_logged == 0){
               bytes_logged += snprintf(&log_buffer[0], 10, "%08ld ", index);
               fileOffset += pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
               bytes_logged += snprintf(&log_buffer[0], 4, "%02x ", msg->log_buf[index]);
               fileOffset += pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
               ++loopCt;
            }
            //place subsequent bytes rd nums and following char
            else if(index % 20 == 0 && index != 0){
               bytes_logged += snprintf(&log_buffer[0], 10, "%08ld ", index);
               fileOffset += pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
               bytes_logged += snprintf(&log_buffer[0], 4, "%02x ", msg->log_buf[index]);
               fileOffset += pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
               ++loopCt;
            }
            //if at last char before newline, place \n at EOL
            else if((index+1) % 20 == 0){
               bytes_logged += snprintf(&log_buffer[0], 4, "%02x\n", msg->log_buf[index]);
               fileOffset += pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
               ++loopCt;
            }
            else if(loopCt == msg->data_len-1){
               bytes_logged += snprintf(&log_buffer[0], 4, "%02x\n", msg->log_buf[index]);
               fileOffset += pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
            }
            //add converted hex char to line
            else{
               bytes_logged += snprintf(&log_buffer[0], 4, "%02x ", msg->log_buf[index]);
               fileOffset += pwrite(logFile_FD, log_buffer, strlen(log_buffer), fileOffset);
               ++loopCt;
            }
         }
         //read in more data and adjust num of bytes read used for while condition
         read_log = read(openedFD, msg->log_buf, READ_BUFF_LEN);
         bytes_log_rd += read_log;
         in_buf_len = sizeof(msg->log_buf);
      }
      fileOffset += pwrite(logFile_FD, one, strlen(one), fileOffset);
   }
   return;
}

void process_req_put(ssize_t client_sockd, struct httpObject* message){//might need to pass in logfile fd
   ssize_t bytes_written = 0;
   ssize_t data_bytes = 0;
   ssize_t bytes_counter_put = 0;
   uint8_t buff_put[READ_BUFF_LEN];
   ssize_t fexist;
   ssize_t bytes_writ_put = 0;

   data_bytes = recv(client_sockd, buff_put, READ_BUFF_LEN, 0);
   bytes_counter_put += data_bytes;
   message->data_len = message->content_len;
   printf("%ld\n", message->data_len);
   int fd_o = open(message->fname, O_CREAT|O_WRONLY|O_TRUNC, 0644);
   message->msg_fd = 0;

   //create stat struct to access file details and permissions
   struct stat finfo;
   fexist = fstat(fd_o, &finfo);
   printf("%ld\n", fexist);


   /*check if file exists, and then check if server has permissions to write file
      if server does not have permissions to write file, return 403. 
      if FNF, return 404
   */
   if(fexist == 0){
      if((finfo.st_mode & S_IWUSR) == S_IWUSR){
         
         //create empty file
         if(data_bytes == 0){
            memcpy(message->log_buf, buff_put, sizeof(buff_put));
            bytes_written = write(fd_o, buff_put, data_bytes);
            
            //set status code, close fd_o, return;
            message->status_code = 201;
            //log the reqS
            close(fd_o);
            return;
         }
         else if(data_bytes == -1){
            //if read returns -1, data is bad and request is bad
            message->status_code = 400;
            //log fail
            return;
         }
         //if amount of data is < buf_len, write once, return 201
         else if(data_bytes < READ_BUFF_LEN){
            memcpy(message->log_buf, buff_put, sizeof(buff_put));
            bytes_written = write(fd_o, buff_put, data_bytes);
            
            if(bytes_written == data_bytes){
               message->status_code = 201;
               close(fd_o);
               return;
            }
            else{
               
               message->status_code = 500;
               close(fd_o);
               return;
            }
         }
         else{
            //loop recv() until all of the data from client has been written to file, return 201 if successful   
            while(data_bytes > 0){
               bytes_written = write(fd_o, buff_put, data_bytes);
               memcpy(message->log_buf, buff_put, sizeof(buff_put));
               bytes_writ_put += bytes_written;
               data_bytes = recv(client_sockd, buff_put, READ_BUFF_LEN, MSG_DONTWAIT);
               memcpy(message->log_buf, buff_put, sizeof(buff_put));
               bytes_counter_put += data_bytes;
               if(bytes_counter_put >= message->content_len){
                  bytes_written = write(fd_o, buff_put, message->content_len - bytes_writ_put);
                  break;
               }
            }
            message->status_code = 201;
            close(fd_o);
            return;
         }
      }
      //else no perms to write
      else{
         message->status_code = 403;
         return;
      }
   }
   //else file DNE
   else{
      message->status_code = 403;
      return;
   }
      
}
/*
   my get functionality works slightly differently than put as i need to send the data back to the client.
   Thus I instead just send the reponse directly from the get function instead of from a separate function.
   This also allows me to send the correct content length without having to modify the struct
*/
void process_req_get(ssize_t client_sockd, struct httpObject* message){
   ssize_t bytes_counter_get = 0;
   ssize_t file_bytes_read = 0;
   uint8_t get_buff_read[READ_BUFF_LEN];
   ssize_t fbyteswrit;
   char response_buf1[100];
   ssize_t fexist1, fullLine;
   char *terminator = "========\n";

   int fd_read = open(message->fname, O_RDONLY);
   message->msg_fd = fd_read;
   //create stat struct to access file details and permissions
   struct stat finfo1;
   fexist1 = fstat(fd_read, &finfo1);
   message->data_len = finfo1.st_size;

   /*check if file exists, and then check if server has permissions to read file
      if server does not have permissions to read file, return 403. 
      If FNF, return 404.
      If log is specified, then log before each dprintf, pass file_size1*3 + len of req into the offset to increment it properly
   */
   if(fexist1 == 0){
      if((finfo1.st_mode & S_IRUSR) == S_IRUSR){
         file_bytes_read = read(fd_read, get_buff_read, READ_BUFF_LEN);
         memcpy(message->log_buf, get_buff_read, sizeof(get_buff_read));
         //acquire offset and increment here
         //if invalid read, return 500
         if(file_bytes_read == -1){
            int x;
            if(logging == 0){
               ssize_t thr_offset1 = global_f_offset;
               ssize_t totalBytes1 = 0;
               snprintf(&response_buf1[0], 100, "FAIL: %s %s %s --- response %d\n", message->method, message->fname_parse, message->httpversion, message->status_code);
               totalBytes1 += strlen(response_buf1) + 9;
               pthread_mutex_lock(&offset_mut);
               global_f_offset += totalBytes1;
               pthread_mutex_unlock(&offset_mut);
               thr_offset1 += pwrite(log_fd, response_buf1, strlen(response_buf1), thr_offset1);
               x+= pwrite(log_fd, terminator, strlen(terminator), thr_offset1);
            }
            dprintf(client_sockd, "%s %d %s%s %d%s", message->httpversion, CODE_SERV_ERR, "Internal Server Error", "Content-Length:", 0, termin);
            close(fd_read);
            return;
         }
         //return empty file
         else if(file_bytes_read == 0){
            if(logging == 0){

               ssize_t thr_offset1 = global_f_offset;

               fullLine = (ssize_t) (floor(message->data_len / 20));
               ssize_t totalBytes1 = fullLine * 69;

               ssize_t extraLine = (message->data_len % 20);
               if (extraLine > 0) {
                  extraLine *= 3;
                  extraLine += 10;
               }
               totalBytes1 += extraLine;
               totalBytes1 += 9;
               sprintf(msg_len, "%ld", message->data_len);
               totalBytes1 += strlen(message->method) + strlen(message->fname_parse) +strlen(msg_len) + 9;
               pthread_mutex_lock(&offset_mut);
               global_f_offset += totalBytes1;
               pthread_mutex_unlock(&offset_mut);

               snprintf(&response_buf1[0], 49, "%s %s length %ld\n", message->method, message->fname_parse, message->data_len);
               thr_offset1 += pwrite(log_fd, response_buf1, strlen(response_buf1), thr_offset1);
               log_req(log_fd, thr_offset1, message->msg_fd, message);
            }
            dprintf(client_sockd, "%s %d %s%s %ld%s", message->httpversion, CODE_SUCCESS, "Ok\r\n", "Content-Length:", message->data_len, termin);
            fbyteswrit = write(client_sockd, get_buff_read, file_bytes_read);
            close(fd_read);
            return;
         }
         //if buffer not full, send
         else if(file_bytes_read < READ_BUFF_LEN){
            if(logging == 0){

               ssize_t thr_offset1 = global_f_offset;

               fullLine = (ssize_t) (floor(message->data_len / 20));
               ssize_t totalBytes1 = fullLine * 69;

               ssize_t extraLine = (message->data_len % 20);
               if (extraLine > 0) {
                  extraLine *= 3;
                  extraLine += 10;
               }
               totalBytes1 += extraLine;
               totalBytes1 += 9;
               sprintf(msg_len, "%ld", message->data_len);
               totalBytes1 += strlen(message->method) + strlen(message->fname_parse) +strlen(msg_len) + 9;
               pthread_mutex_lock(&offset_mut);
               global_f_offset += totalBytes1;
               pthread_mutex_unlock(&offset_mut);

               snprintf(&response_buf1[0], 49, "%s %s length %ld\n", message->method, message->fname_parse, message->data_len);
               thr_offset1 += pwrite(log_fd, response_buf1, strlen(response_buf1), thr_offset1);
               log_req(log_fd, thr_offset1, message->msg_fd, message);
            }
            dprintf(client_sockd, "%s %d %s%s %ld%s", message->httpversion, CODE_SUCCESS, "Ok\r\n", "Content-Length:", message->data_len, termin);
            fbyteswrit = write(client_sockd, get_buff_read, file_bytes_read);
            close(fd_read);
            return;
         }
         else{
            if(logging == 0){

               ssize_t thr_offset1 = global_f_offset;

               fullLine = (ssize_t) (floor(message->data_len / 20));
               ssize_t totalBytes1 = fullLine * 69;

               ssize_t extraLine = (message->data_len % 20);
               if (extraLine > 0) {
                  extraLine *= 3;
                  extraLine += 10;
               }
               totalBytes1 += extraLine;
               totalBytes1 += 9;
               sprintf(msg_len, "%ld", message->data_len);
               totalBytes1 += strlen(message->method) + strlen(message->fname_parse) +strlen(msg_len) + 9;
               pthread_mutex_lock(&offset_mut);
               global_f_offset += totalBytes1;
               pthread_mutex_unlock(&offset_mut);

               snprintf(&response_buf1[0], 49, "%s %s length %ld\n", message->method, message->fname_parse, message->data_len);
               thr_offset1 += pwrite(log_fd, response_buf1, strlen(response_buf1), thr_offset1);
               log_req(log_fd, thr_offset1, message->msg_fd, message);
            }
            dprintf(client_sockd, "%s %d %s%s %ld%s", message->httpversion, CODE_SUCCESS, "Ok\r\n", "Content-Length:", message->data_len, termin);
            //loop through the file and write out all data from file, up to length given by fstat.
            while((bytes_counter_get <= message->data_len) || (file_bytes_read > 0)){
               fbyteswrit = write(client_sockd, get_buff_read, file_bytes_read);
               if(fbyteswrit != file_bytes_read){
                  break;
               }
               file_bytes_read = read(fd_read, get_buff_read, READ_BUFF_LEN);
               bytes_counter_get += file_bytes_read;
            }
            close(fd_read);
            return;
         }
      }
      else{
         int x;
         if(logging == 0){
            
            ssize_t thr_offset1 = global_f_offset;
            ssize_t totalBytes1 = 0;
            snprintf(&response_buf1[0], 100, "FAIL: %s %s %s --- response %d\n", message->method, message->fname_parse, message->httpversion, message->status_code);
            totalBytes1 += strlen(response_buf1) + 9;
            pthread_mutex_lock(&offset_mut);
            global_f_offset += totalBytes1;
            pthread_mutex_unlock(&offset_mut);
            thr_offset1 += pwrite(log_fd, response_buf1, strlen(response_buf1), thr_offset1);
            x+= pwrite(log_fd, terminator, strlen(terminator), thr_offset1);
         }
         dprintf(client_sockd, "%s %d %s%s %d%s", message->httpversion, CODE_FORBID, "Forbidden\r\n", "Content-Length:", 0, termin);
         return;
      }
   }
   else{
      int x;
      if(logging == 0){
         
         ssize_t thr_offset1 = global_f_offset;
         ssize_t totalBytes1 = 0;
         snprintf(&response_buf1[0], 100, "FAIL: %s %s %s --- response %d\n", message->method, message->fname_parse, message->httpversion, message->status_code);
         totalBytes1 += strlen(response_buf1) + 9;
         pthread_mutex_lock(&offset_mut);
         global_f_offset += totalBytes1;
         pthread_mutex_unlock(&offset_mut);
         thr_offset1 += pwrite(log_fd, response_buf1, strlen(response_buf1), thr_offset1);
         x+= pwrite(log_fd, terminator, strlen(terminator), thr_offset1);
      }
      dprintf(client_sockd, "%s %d %s%s %d%s", message->httpversion, CODE_FILE_NOT_FOUND, "Not Found\r\n", "Content-Length:", 0, termin);
      return;
   }
}

void process_req_head(struct httpObject* message){
   ssize_t file_size2;
   int fd_head = open(message->fname, O_RDONLY);
   message->msg_fd = fd_head;
   ssize_t fexist2;

   struct stat finfo2;
   fexist2 = fstat(fd_head, &finfo2);
   file_size2 = finfo2.st_size;
   message->data_len = file_size2;
   //check if file extsts and if server has correct permissions, otherwise send err
   if(fexist2 == 0){   
      //if file exists, check perms
      if((finfo2.st_mode & S_IRUSR) == S_IRUSR){
         message->status_code = CODE_SUCCESS;
         //dprintf(client_sockd, "%s %d %s%s %ld%s", message->httpversion, CODE_SUCCESS, "Ok\r\n", "Content-Length:", file_size2, termin);
         close(fd_head);
         return;               
      }
      //no perms to read from file
      else{
         message->status_code = CODE_FORBID;
         //dprintf(client_sockd, "%s %d %s%s %d%s", message->httpversion, CODE_FORBID, "Forbidden\r\n", "Content-Length:", 0, termin);
         close(fd_head);
         return;
      }
   }
   //file not found
   else{
      message->status_code = CODE_FILE_NOT_FOUND;
      //dprintf(client_sockd, "%s %d %s%s %d%s", message->httpversion, CODE_FILE_NOT_FOUND, "Not Found\r\n", "Content-Length:", 0, termin);
      return;
   }
}

/*
   take in the message obj. Then send response to the client_sockd according to the status
   code set in the process_req
*/
void send_response(ssize_t client_sockd, struct httpObject* message){
   if(message->status_code == 200){
      dprintf(client_sockd, "%s %d %s%s %ld%s", message->httpversion, CODE_SUCCESS, "Ok\r\n", "Content-Length:", message->data_len, termin);
      close(client_sockd);
      return;
   }
   else if(message->status_code == 201){
      dprintf(client_sockd, "%s %d %s%s %d%s", message->httpversion, CODE_CREATED, "Created\r\n", "Content-Length:", 0, termin);
      close(client_sockd);
      return;
   }
   else if(message->status_code == 400){
      dprintf(client_sockd, "%s %d %s%s %d%s", message->httpversion, CODE_BAD_REQ, "Bad Request\r\n", "Content-Length:", 0, termin);
      close(client_sockd);
      return;
   }
   else if(message->status_code == 403){
      dprintf(client_sockd, "%s %d %s%s %d%s", message->httpversion, CODE_FORBID, "Forbidden\r\n", "Content-Length:", 0, termin);
      close(client_sockd);
      return;
   }
   else if(message->status_code == 404){
      dprintf(client_sockd, "%s %d %s%s %d%s", message->httpversion, CODE_FILE_NOT_FOUND, "Not Found\r\n", "Content-Length:", 0, termin);
      close(client_sockd);
      return;
   }
   else if(message->status_code == 500){
      dprintf(client_sockd, "%s %d %s%s %d%s", message->httpversion, CODE_SERV_ERR, "Internal Server Error", "Content-Length:", 0, termin);
      close(client_sockd);
      return;
   }
}

//Function to call recv() until the buffer is full or contains the termin str
ssize_t recv_full(ssize_t client_sockd, struct httpObject* message){
   ssize_t bytes_recv = 0;
   printf("%s\n", message->buffer);
   ssize_t ret1 = recv(client_sockd, message->buffer, MAX_HEADER_SIZE, 0);
   bytes_recv += ret1;


   if(strstr(message->buffer, termin) != NULL){
      return bytes_recv;
   }
   else if(ret1 < 0){
      return ret1;
   }
   else{
      while((bytes_recv <= MAX_HEADER_SIZE)){
         ret1 = recv(client_sockd, message->buffer+bytes_recv, MAX_HEADER_SIZE-bytes_recv, 0);
         bytes_recv += ret1;
         if(ret1 < 0){
            return ret1;
         }
         else if(ret1 == 0){
            if(strstr(message->buffer, termin) == NULL){
               return -1;
            }
         }
         else{
            if(strstr(message->buffer, termin) != NULL){
               return bytes_recv;
            }
            else{
               continue;
            }
         }
      }
      return bytes_recv;
   }
   
}



/*
   Enqueue for the circular buffer used to store client sockets awaiting a thread to run their request.
   Lock the thread, check for a full queue, if full, wait. Othwerwise, add client socket to queue and increment
   the head pointer accordingly. Send a condition variable signal to alert the dequeue function it can dequeue,
   if it is waiting.
*/
void CBuff_SeqEnqueue(CircularBuffer *pcb, int client_socket){
   //if queue is full, wait for dequeue signal use pthread_cond_wait
   while(pcb->pq_size == pcb->max_size){
      pthread_mutex_unlock(&pcb->mut);
      usleep(20);
      pthread_mutex_lock(&pcb->mut);
   } 

   pcb->pInt_queue[pcb->head] = client_socket;
   pcb->pq_size += 1;
   printf("item enqueued...\n");

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
   printf("head...%d\ntail...%d\n", pcb->pInt_queue[pcb->head], pcb->pInt_queue[pcb->tail]);
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
   printf("item dequeued...\nsize: %ld\n", pcb->pq_size);
   //if head tail has reached end, reset to beginning. Otherwise, increment tail pointer
   if(pcb->tail == pcb->max_size){
      pcb->tail = pcb->pInt_queue[0];
   }
   else{
      pcb->tail += 1;
   }
   printf("head...%d\ntail...%d\n", pcb->pInt_queue[pcb->head], pcb->pInt_queue[pcb->tail]);
   return value;
}

/*
   Each thread will be created and then initially wait. Once the thread picks up the signal from dequeue,
   it will run the request from the dequeued client socket. This is the same functionality as the server
   from asgn1. 
*/
void* thread_func(void *obj){
   ThreadArg *parg = (ThreadArg*) obj;
   CircularBuffer *pcb = parg->pcb;
   ssize_t fullLine;
   int thr_offset;
   char response_buf[100];
   char *terminator = "========\n";

   /*
      If a log file is specified, we will use pwrite to allow each thread to write to the log file.
      The file must be created with O_TRUNC. 
      In order for this to be done, we will have a global file with the fd passed in through the 
      ThreadArg. 
   */

   struct httpObject message;

   //we must lock the thread in order to call wait()
   pthread_mutex_lock(&pcb->mut);
   /*
      We will call wait on each thread if the queue is empty, and then wait for a signal from enqueue to continue.
      
   */
   while(pcb->pq_size == 0){
      pthread_cond_wait(&pcb->cond, &pcb->mut); 
   }

   /*
      Once we continue, this thread has acquired the lock in order to dequeue an element.
      We then dequeue, unlock the lock, and send a signal that the circ_buffer is free.
   */

   int client_sockd1 = CBuff_SeqDequeue(pcb);
   pthread_cond_signal(&pcb->cond);
   pthread_mutex_unlock(&pcb->mut);
   
   message.c_sock = client_sockd1;

   ssize_t bytes = recv_full(client_sockd1, &message);//should be call to recv full
   //check for split headers
   message.buffer[bytes] = 0; // null terminate
   printf("[+] received %ld bytes from client\n[+] response: ", bytes);
   ssize_t written2 = write(STDOUT_FILENO, message.buffer, bytes);
   if(written2 != bytes){
   }


   read_http_req(client_sockd1, &message);
   if(strcmp(message.httpversion, "HTTP/1.1") != 0){
      int x;
      if(logging == 0){
            thr_offset = global_f_offset;
            ssize_t totalBytes = 0;
            
            snprintf(&response_buf[0], 100, "FAIL: %s %s %s --- response %d\n", message.method, message.fname_parse, message.httpversion, message.status_code);
            totalBytes += strlen(response_buf) + 9;
            pthread_mutex_lock(&offset_mut);
            global_f_offset += totalBytes;
            pthread_mutex_unlock(&offset_mut);
            thr_offset += pwrite(log_fd, response_buf, strlen(response_buf), thr_offset);
            x+= pwrite(log_fd, terminator, strlen(terminator), thr_offset);
         }
      
      dprintf(client_sockd1, "HTTP/1.1 %d %s%s %d%s", CODE_BAD_REQ, "Bad Request\r\n", "Content-Length:", 0, termin);
      close(client_sockd1);
      goto AFTER;
   }
   //error checking the header parse
   if(strlen(message.fname_parse) > 28){
      
      if(logging == 0){
         int x;
         thr_offset = global_f_offset;
         ssize_t totalBytes = 0;
         
         snprintf(&response_buf[0], 100, "FAIL: %s %s %s --- response %d\n", message.method, message.fname_parse, message.httpversion, message.status_code);
         totalBytes += strlen(response_buf) + 9;
         pthread_mutex_lock(&offset_mut);
         global_f_offset += totalBytes;
         pthread_mutex_unlock(&offset_mut);
         thr_offset += pwrite(log_fd, response_buf, strlen(response_buf), thr_offset);
         x+= pwrite(log_fd, terminator, strlen(terminator), thr_offset);
         }
      
      dprintf(client_sockd1, "%s %d %s%s %d%s", message.httpversion, CODE_BAD_REQ, "Bad Request\r\n", "Content-Length:", 0, termin);
      close(client_sockd1);
      goto AFTER;   
   }
   if(strspn(message.fname,valid_chars) != strlen(message.fname)){

      if(logging == 0){
         int x;
         thr_offset = global_f_offset;
         ssize_t totalBytes = 0;
         
         snprintf(&response_buf[0], 100, "FAIL: %s %s %s --- response %d\n", message.method, message.fname_parse, message.httpversion, message.status_code);
         totalBytes += strlen(response_buf) + 9;
         pthread_mutex_lock(&offset_mut);
         global_f_offset += totalBytes;
         pthread_mutex_unlock(&offset_mut);
         thr_offset += pwrite(log_fd, response_buf, strlen(response_buf), thr_offset);
         x+= pwrite(log_fd, terminator, strlen(terminator), thr_offset);
         }
      
      dprintf(client_sockd1, "%s %d %s%s %d%s", message.httpversion, CODE_BAD_REQ, "Bad Request\r\n", "Content-Length:", 0, termin);
      close(client_sockd1);
      goto AFTER;
   }

   /*
      This is the functionality of the program. It handles GET, PUT, and HEAD requests.
      Prints out apporpraite messages to the client but will also log the outcomes.

   */
   
   

   if(strcmp(message.method, "PUT") == 0){
      process_req_put(client_sockd1, &message);

      if(logging == 0){
         int x = 0;
         if(message.status_code == 201){
            
            thr_offset = global_f_offset;


            fullLine = (ssize_t) (floor(message.data_len / 20));
            ssize_t totalBytes = fullLine * 69;

            ssize_t extraLine = (message.data_len % 20);
            if (extraLine > 0) {
               extraLine *= 3;
               extraLine += 10;
            }
            totalBytes += extraLine;
            totalBytes += 9;
            snprintf(&response_buf[0], 49, "%s %s length %ld\n", message.method, message.fname_parse, message.data_len);
            totalBytes += strlen(response_buf);

            pthread_mutex_lock(&offset_mut);
            global_f_offset += totalBytes;

            pthread_mutex_unlock(&offset_mut);

            

            int pr = pwrite(log_fd, response_buf, strlen(response_buf), thr_offset);
            thr_offset += pr;

            log_req(log_fd, thr_offset, message.msg_fd, &message);

         }
         else{
            
            thr_offset = global_f_offset;
            ssize_t totalBytes = 0;
            
            snprintf(&response_buf[0], 100, "FAIL: %s %s %s --- response %d\n", message.method, message.fname_parse, message.httpversion, message.status_code);
            totalBytes += strlen(response_buf) + 9;
            pthread_mutex_lock(&offset_mut);
            global_f_offset += totalBytes;
            pthread_mutex_unlock(&offset_mut);
            thr_offset += pwrite(log_fd, response_buf, strlen(response_buf), thr_offset);
            x+= pwrite(log_fd, terminator, strlen(terminator), thr_offset);
         }
         
      }
      send_response(client_sockd1, &message);
      close(client_sockd1);
   }
   else if(strcmp(message.method, "GET")==0){
      process_req_get(client_sockd1, &message);
      close(client_sockd1);
   }
   else if(strcmp(message.method, "HEAD")==0){
      process_req_head(&message);
      if(logging == 0){
         int x = 0;
         if(message.status_code == 201){
            
            thr_offset = global_f_offset;


            fullLine = (ssize_t) (floor(message.data_len / 20));
            ssize_t totalBytes = fullLine * 69;

            ssize_t extraLine = (message.data_len % 20);
            if (extraLine > 0) {
               extraLine *= 3;
               extraLine += 10;
            }
            totalBytes += extraLine;
            totalBytes += 9;
            snprintf(&response_buf[0], 49, "%s %s length %ld\n", message.method, message.fname_parse, message.data_len);
            totalBytes += strlen(response_buf);

            pthread_mutex_lock(&offset_mut);
            global_f_offset += totalBytes;

            pthread_mutex_unlock(&offset_mut);

            

            int pr = pwrite(log_fd, response_buf, strlen(response_buf), thr_offset);
            thr_offset += pr;

            log_req(log_fd, thr_offset, message.msg_fd, &message);
         }
         else{
            thr_offset = global_f_offset;
            ssize_t totalBytes = 0;
            
            snprintf(&response_buf[0], 100, "FAIL: %s %s %s --- response %d\n", message.method, message.fname_parse, message.httpversion, message.status_code);
            totalBytes += strlen(response_buf) + 9;
            pthread_mutex_lock(&offset_mut);
            global_f_offset += totalBytes;
            pthread_mutex_unlock(&offset_mut);
            thr_offset += pwrite(log_fd, response_buf, strlen(response_buf), thr_offset);
            x+= pwrite(log_fd, terminator, strlen(terminator), thr_offset);
         }
         
      }
      send_response(client_sockd1, &message);
      close(client_sockd1);
   }

   
   AFTER:
      return 0;
   return 0;
}



int main(int argc, char** argv) {
   /*
   Parse argv using getopt() to parse out the port, the num threads, and log file
   */
   global_f_offset = 0;
   logging = 1;
   char *port = "";
   int num_threads = DEFAULT_THR;
   char *thr = "";
   char *logF;
   //log_fd = open(argv[2], O_RDWR|O_CREAT|O_TRUNC, 0644);
   int opt_ret;


   while((opt_ret = getopt(argc, argv, "N:l:")) != -1){
      switch(opt_ret){
         case 'N':
            thr = optarg;
            break;
         case 'l':
            logF = optarg;
            log_fd = open(logF, O_CREAT|O_RDWR|O_TRUNC, 0644);
            logging = 0;
            break;
         case '?':
            return -1;
         default:
            break;
      }
   }
   if(thr != NULL){
      sscanf(thr, "%d", &num_threads);
   }
   if(port != NULL){
      port = argv[optind];
   }
   else{
      return -1;
   }


printf("logfile: %s\nlog fd: %d\n", logF, log_fd);

   printf("%d\n", logging);
   struct sockaddr_in server_addr;
   memset(&server_addr, 0, sizeof(server_addr));
   server_addr.sin_family = AF_INET;
   server_addr.sin_port = htons(atoi(port));
   server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
   socklen_t addrlen = sizeof(server_addr);

   /*
   Create server socket
   */
   int server_sockd = socket(AF_INET, SOCK_STREAM, 0);

   // Need to check if server_sockd < 0, meaning an error
   if (server_sockd < 0) {
      perror("socket");
   }

   /*
   Configure server socket
   */
   int enable = 1;

   /*
   This allows you to avoid: 'Bind: Address Already in Use' error
   */
   int ret = setsockopt(server_sockd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

   /*
   Bind server address to socket that is open
   */
   ret = bind(server_sockd, (struct sockaddr *) &server_addr, addrlen);

   /*
   Listen for incoming connections
   */
   ret = listen(server_sockd, SOMAXCONN); // 5 should be enough, if not use SOMAXCONN

   if (ret < 0) {
      return 1;
   }

   struct sockaddr client_addr;
   socklen_t client_addrlen;

   
   CircularBuffer *cb = malloc(sizeof(CircularBuffer));
   cb->pInt_queue = malloc(sizeof(int) * num_threads);
   cb->head = 0;
   cb->tail = 0;
   cb->max_size = num_threads;
   int cond_init = pthread_cond_init(&cb->cond, NULL);

   pthread_mutex_init(&cb->mut, NULL);
   pthread_t thread_ids[num_threads];
   ThreadArg args;
   args.pcb = cb;
   

   if(cond_init < 0){
      printf("err");
      return 1;
   }

   

   //create thread pool
   for(ssize_t idx = 0; idx < num_threads; ++idx){
      pthread_create(&thread_ids[idx], NULL, thread_func, &args);
   }

   while(1){
      //if queue full, pthread_cond_wait

      while(cb->pq_size >= cb->max_size){
         pthread_cond_wait(&cb->cond, &cb->mut);
      }

      int client_sockd = accept(server_sockd, &client_addr, &client_addrlen);
      printf("csok: %d\n", client_sockd);
      pthread_mutex_lock(&cb->mut);
      CBuff_SeqEnqueue(cb, client_sockd);
      pthread_cond_signal(&cb->cond);
      pthread_mutex_unlock(&cb->mut);
      
   }
   /*
      end of thread workload
   */
   // for(ssize_t indx = 0; indx < num_threads; ++indx){
   //    pthread_join(thread_ids[indx], NULL);
   // }
   //
   close(server_sockd);
   return 0;
}