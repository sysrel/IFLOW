#include <stdlib.h>
#include <stdio.h>
#include <string.h>

char x = '5';
char y = '3';

char *receive(char *buf, char *temp) {
  *temp = *buf;
  *buf = 'A';
  return buf;
}


int main(int argc, char **argv) {

    char *buf1 = &x;
    char *buf2 = (char*)malloc(10);
    char *buf3 = (char*)malloc(10);
    char *buf4 = (char*)malloc(10);
    char *b = receive(buf1, buf4);
    int length = *buf1;
    memcpy(buf2, b, buf1 - buf2);
    memcpy(buf3, buf4, length);
    memcpy(buf4, b, &buf1[0]);
    return 0;
}

