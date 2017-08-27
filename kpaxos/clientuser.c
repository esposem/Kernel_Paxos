#include<stdio.h>
#include<stdlib.h>
#include<errno.h>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>
#include <sys/time.h>

#define BUFFER_LENGTH 3000
static char receive[BUFFER_LENGTH];

void unpack_message(char * msg){
  struct timeval * t = (struct timeval *) msg;
  msg += sizeof(struct timeval);
  int iid = *msg;
  msg += sizeof(int);
  char * mess = msg;
  printf("time %ld : %ld, iid %d, message %s\n",t->tv_sec, t->tv_usec, iid, mess);
}


int main(int argc, char const *argv[]) {
  int ret, fd;
  char stringToSend[BUFFER_LENGTH];
  printf("Starting device test code example...\n");
  fd = open("/dev/chardevice/kclient0", O_RDWR);             // Open the device with read/write access
   if (fd < 0){
    perror("Failed to open the device...\nTerminated\n");
    return errno;
  }

  // printf("Type in a short string to send to the kernel module:\n");
  // scanf("%[^\n]%*c", stringToSend);                // Read in a string (with spaces)
  // printf("Writing message to the device [%s].\n", stringToSend);
  //  ret = write(fd, stringToSend, strlen(stringToSend)); // Send the string to the LKM, offset of file increased by strlen(stringToSend)
  // if (ret < 0){
  //   perror("Failed to write the message to the device.\nTerminated\n");
  //   return errno;
  // }
  //
  // printf("Press ENTER to read back from the device...\n");
  // getchar();
  int i = 0;
  while(1){
    ret = read(fd, receive, BUFFER_LENGTH);        // Read the response from the LKM, start reading from offset not beginning
    if (ret >= 0){
      unpack_message(receive);
      printf("read %s %d\n", receive, i);
    }
    i++;
  }

  close(fd);
  printf("End of the program\n");
  return 0;
}
