
# Image Compression Client-Server in C

## Purpose
This project provides a simple image compression client-server system written in C. Unlike many available image compressors, which are either complicated or have limitations, this implementation offers a straightforward and effective solution using standard libraries and multithreading.

# Compile the server
gcc -O2 Image_compress_server.c -o server -pthread -ljpeg

# Compile the client
gcc -O2 Image_compress_client.c -o client -pthread -ljpeg

# Run the server (in one terminal)
./server 19292

# Run each client in separate terminals
./client 127.0.0.1 19292 image1.jpg image2.jpg


## Usage
- Start the server in one terminal specifying a port, for example:
./server 19292

- Run each client in a separate terminal, specifying server IP, port, and files to compress, for example:
./client 127.0.0.1 19292 image1.jpg image2.jpg


Each client runs in its own terminal window and connects to the server for parallel image compression.

---

This tool fills a gap for easy-to-use, efficient image compression in C without complicated dependencies or limitations.
