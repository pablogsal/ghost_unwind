#include <unistd.h>
#include <fcntl.h>
#include <iostream>

void read_file() {
    char buf[100];
    int fd = open("/etc/hostname", O_RDONLY);
    if (fd != -1) {
        read(fd, buf, sizeof(buf));
        close(fd);
    }
}

int main() {
    std::cout << "Testing read interception..." << std::endl;
    read_file();
    return 0;
}
