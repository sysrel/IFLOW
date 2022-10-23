#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct ssl_ctx {
  char *data;
  int size;
};


void TLS_Init(struct ssl_ctx **p) {
  *p = (struct ssl_ctx*) malloc(sizeof(struct ssl_ctx));
}


int mbedtls_ssl_handshake(struct ssl_ctx *p) {
  if (p->size > 0)
     return 0;
  else return 2;
}

int mbedtls_ssl_read(struct ssl_ctx *p) {
   return p->size;
}

int TLS_Connect(struct ssl_ctx *p) {
   return mbedtls_ssl_handshake(p); 
}

int SOCKETS_Connect() {
  struct ssl_ctx *sp;
  TLS_init(&sp);
  int res = TLS_Connect(sp);
  if (res == 0 || res == 2) {
     int lengthc = mbedtls_ssl_read(sp);
     return 0; 
  }
  else  {
     int lengthcor = mbedtls_ssl_read(sp);
     return 1;
  }
}


int main(int argc, char **argv) {
    SOCKETS_Connect();
    return 0;
}

