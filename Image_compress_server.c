// Compile: gcc -O2 Image_compress_server.c -o server -pthread -ljpeg
// Usage:   ./server <port>(19292)

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <jpeglib.h>
#include <sys/stat.h>
#include <pthread.h>

#define BACKLOG 50

// read exactly n bytes
ssize_t readn(int fd, void *buf, size_t n) {
    size_t left = n;
    char *p = buf;
    while (left) {
        ssize_t r = recv(fd, p, left, 0);
        if (r <= 0) return r;
        left -= r;
        p += r;
    }
    return n;
}

// write exactly n bytes
ssize_t writen(int fd, const void *buf, size_t n) {
    size_t left = n;
    const char *p = buf;
    while (left) {
        ssize_t w = send(fd, p, left, 0);
        if (w <= 0) return w;
        left -= w;
        p += w;
    }
    return n;
}

// Simplified JPEG downsample + recompress
int process_and_compress_file(const char *inpath, const char *outpath, int quality) {
    FILE *in = fopen(inpath, "rb");
    if (!in) { perror("in"); return -1; }

    struct jpeg_decompress_struct dinfo;
    struct jpeg_error_mgr jerr;
    dinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&dinfo);
    jpeg_stdio_src(&dinfo, in);

    if (jpeg_read_header(&dinfo, TRUE) != JPEG_HEADER_OK) {
        fclose(in);
        jpeg_destroy_decompress(&dinfo);
        return -1;
    }

    jpeg_start_decompress(&dinfo);

    int width = dinfo.output_width;
    int height = dinfo.output_height;
    int comps = dinfo.output_components;

    if (comps != 3) {
        jpeg_finish_decompress(&dinfo);
        jpeg_destroy_decompress(&dinfo);
        fclose(in);
        return -1;
    }

    unsigned char *inbuf = malloc((size_t)width * height * 3);
    if (!inbuf) { fclose(in); return -1; }

    while (dinfo.output_scanline < height) {
        unsigned char *row = inbuf + (size_t)dinfo.output_scanline * width * 3;
        jpeg_read_scanlines(&dinfo, &row, 1);
    }

    jpeg_finish_decompress(&dinfo);
    jpeg_destroy_decompress(&dinfo);
    fclose(in);

    int neww = width / 2;
    int newh = height / 2;

    unsigned char *outbuf = malloc((size_t)neww * newh * 3);
    if (!outbuf) { free(inbuf); return -1; }

    // Downsample 2×2 → 1 pixel
    for (int y = 0; y < newh; y++) {
        for (int x = 0; x < neww; x++) {
            int r = 0, g = 0, b = 0;
            int sx = x * 2, sy = y * 2;

            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) {
                    size_t idx = ((size_t)(sy + dy) * width + (sx + dx)) * 3;
                    r += inbuf[idx];
                    g += inbuf[idx + 1];
                    b += inbuf[idx + 2];
                }

            size_t oidx = ((size_t)y * neww + x) * 3;
            outbuf[oidx]     = r / 4;
            outbuf[oidx + 1] = g / 4;
            outbuf[oidx + 2] = b / 4;
        }
    }

    free(inbuf);

    // Write JPEG
    FILE *out = fopen(outpath, "wb");
    if (!out) { free(outbuf); return -1; }

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr2;
    cinfo.err = jpeg_std_error(&jerr2);
    jpeg_create_compress(&cinfo);

    jpeg_stdio_dest(&cinfo, out);

    cinfo.image_width = neww;
    cinfo.image_height = newh;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    jpeg_start_compress(&cinfo, TRUE);

    int row_stride = neww * 3;
    while (cinfo.next_scanline < cinfo.image_height) {
        unsigned char *row = outbuf + cinfo.next_scanline * row_stride;
        jpeg_write_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(out);
    free(outbuf);
    return 0;
}

void handle_client(int clientfd) {
    uint32_t nf_net;
    if (readn(clientfd, &nf_net, 4) != 4) {
        close(clientfd);
        return;
    }

    uint32_t nf = ntohl(nf_net);

    for (uint32_t i = 0; i < nf; i++) {

        // filename length
        uint32_t fn_len_net;
        if (readn(clientfd, &fn_len_net, 4) != 4) { close(clientfd); return; }
        uint32_t fn_len = ntohl(fn_len_net);

        char *fname = malloc(fn_len + 1);
        if (!fname) { close(clientfd); return; }
        readn(clientfd, fname, fn_len);
        fname[fn_len] = '\0';

        // 64-bit file size split
        uint32_t hi_net, lo_net;
        readn(clientfd, &hi_net, 4);
        readn(clientfd, &lo_net, 4);

        uint64_t fsize = ((uint64_t)ntohl(hi_net) << 32) | ntohl(lo_net);

        // Unique TMP filenames per thread
        char tmp_in[128], tmp_out[128];
        snprintf(tmp_in, sizeof(tmp_in), "/tmp/srv_in_%ld.jpg", pthread_self());
        snprintf(tmp_out, sizeof(tmp_out), "/tmp/srv_out_%ld.jpg", pthread_self());

        FILE *tmp = fopen(tmp_in, "wb");
        if (!tmp) { free(fname); close(clientfd); return; }

        uint64_t remaining = fsize;
        char buffer[65536];

        while (remaining) {
            size_t to_read = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
            readn(clientfd, buffer, to_read);
            fwrite(buffer, 1, to_read, tmp);
            remaining -= to_read;
        }
        fclose(tmp);
        free(fname);

        if (process_and_compress_file(tmp_in, tmp_out, 30) != 0) {
            uint32_t z = htonl(0);
            writen(clientfd, &z, 4);
            writen(clientfd, &z, 4);
            continue;
        }

        FILE *cf = fopen(tmp_out, "rb");
        fseek(cf, 0, SEEK_END);
        uint64_t csz = ftell(cf);
        fseek(cf, 0, SEEK_SET);

        uint32_t hi = htonl((uint32_t)(csz >> 32));
        uint32_t lo = htonl((uint32_t)(csz & 0xffffffff));

        writen(clientfd, &hi, 4);
        writen(clientfd, &lo, 4);

        while (1) {
            size_t r = fread(buffer, 1, sizeof(buffer), cf);
            if (r == 0) break;
            writen(clientfd, buffer, r);
        }

        fclose(cf);
        remove(tmp_in);
        remove(tmp_out);
    }

    close(clientfd);
}

void *thread_worker(void *arg) {
    int clientfd = *(int*)arg;
    free(arg);

    handle_client(clientfd);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv;
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port = htons(port);

    bind(sock, (struct sockaddr *)&srv, sizeof(srv));
    listen(sock, BACKLOG);

    printf("Multithreaded server listening on port %d\n", port);

    while (1) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);

        int c = accept(sock, (struct sockaddr *)&cli, &clilen);
        if (c < 0) continue;

        printf("Client connected: %s\n", inet_ntoa(cli.sin_addr));

        pthread_t t;
        int *pclient = malloc(sizeof(int));
        *pclient = c;

        pthread_create(&t, NULL, thread_worker, pclient);
        pthread_detach(t);
    }

    close(sock);
    return 0;
}
