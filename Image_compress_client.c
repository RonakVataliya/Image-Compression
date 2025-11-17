// Compile: gcc -O2 Image_compress_client.c -o client -pthread -ljpeg
// Usage:   ./server <IP_address>(127.0.0.1) <port>(19292)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>

ssize_t readn(int fd, void *buf, size_t n) {
    size_t left=n; char *p=buf;
    while(left){
        ssize_t r = recv(fd,p,left,0);
        if(r<=0) return r;
        left-=r; p+=r;
    }
    return n;
}

ssize_t writen(int fd, const void *buf, size_t n) {
    size_t left=n; const char *p=buf;
    while(left){
        ssize_t w = send(fd,p,left,0);
        if(w<=0) return w;
        left-=w; p+=w;
    }
    return n;
}

int main(int argc,char **argv){
    if(argc < 4){
        printf("Usage: %s <server-ip> <port> <file1.jpg> <file2.jpg> ...\n",argv[0]);
        return 1;
    }

    char *ip = argv[1];
    int port = atoi(argv[2]);
    int nf = argc - 3;

    int sock = socket(AF_INET,SOCK_STREAM,0);
    if(sock < 0){ perror("socket"); return 1; }

    struct sockaddr_in serv;
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serv.sin_addr);

    if(connect(sock,(struct sockaddr*)&serv,sizeof(serv))<0){
        perror("connect");
        return 1;
    }

    printf("Connected. Sending %d files...\n", nf);

    uint32_t nf_net = htonl(nf);
    writen(sock, &nf_net, 4);

    for(int k=3; k<argc; k++){
        char *filepath = argv[k];

        FILE *f = fopen(filepath,"rb");
        if(!f){ perror("fopen"); return 1; }

        fseek(f,0,SEEK_END);
        uint64_t fsize = ftell(f);
        fseek(f,0,SEEK_SET);

        unsigned char *data = malloc(fsize);
        size_t read_bytes = fread(data, 1, fsize, f);
        if (read_bytes != fsize) 
        {
          fprintf(stderr, "Error reading file %s: only read %zu bytes out of %lu\n", filepath, read_bytes, (unsigned long)fsize);
          free(data);
          fclose(f);
          return 1;
        }

        fclose(f);

        // send filename
        uint32_t fn_len = strlen(filepath);
        uint32_t fn_len_net = htonl(fn_len);
        writen(sock,&fn_len_net,4);
        writen(sock,filepath,fn_len);

        // send size
        uint32_t hi = htonl((uint32_t)(fsize>>32));
        uint32_t lo = htonl((uint32_t)(fsize & 0xffffffff));
        writen(sock,&hi,4);
        writen(sock,&lo,4);

        // send file data
        uint64_t sent=0;
        while(sent < fsize){
            size_t chunk = (fsize-sent > 65536)?65536:(fsize-sent);
            writen(sock,data+sent,chunk);
            sent+=chunk;
        }
        free(data);
        printf("Sent: %s (%lu bytes)\n", filepath, (unsigned long)fsize);

        // receive compressed output
        uint32_t chi,clo;
        readn(sock,&chi,4);
        readn(sock,&clo,4);
        uint64_t csize = ((uint64_t)ntohl(chi)<<32) | ntohl(clo);

        unsigned char *out = malloc(csize);
        readn(sock,out,csize);

        // create output file name
        char outname[300];
        snprintf(outname,sizeof(outname), "recv_%s", strrchr(filepath,'/') ? strrchr(filepath,'/')+1 : filepath);

        FILE *o = fopen(outname,"wb");
        fwrite(out,1,csize,o);
        fclose(o);

        free(out);
        printf("Received compressed file: %s (%lu bytes)\n", outname, (unsigned long)csize);
    }

    close(sock);
    return 0;
}