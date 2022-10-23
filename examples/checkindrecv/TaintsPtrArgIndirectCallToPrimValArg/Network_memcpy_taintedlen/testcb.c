#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct Network {
  int numtries;
  void (*recv)(char *buf, int *size);
};


struct Network nw1;
struct Network *nw3;


void iot_receive(char *buf, int *size) {
  buf[1] = 'b';
  *size = 1;
}


void registerCallback(struct Network *n, void (*r)(char *,int*)) {
   n->recv = r;
}

void bar(struct Network *n, char *buf) {
   int i;
   n->recv(buf,&i);
   printf("received %d bytes\n", i);
}

void foo(struct Network *n, char *buf) {
   bar(n, buf);   
}


void copy(char *buf1, char *buf2) {
   memcpy(buf2, buf1, buf2[1]);
}


char topbuf[10];

int main(int argc, char **argv) {

    struct Network *nw2;
    nw2 = (struct Network*)malloc(sizeof(struct Network));
    registerCallback(nw2, iot_receive);
    char *buf1 = (char*)malloc(10);
    char *buf2 = (char*)malloc(10);
    char *buf3 = (char*)malloc(10);
    char *buf4 = (char*)malloc(10);
    int n; 
    foo(nw2, buf2);
    copy(buf1, buf2);
    return 0;
}

