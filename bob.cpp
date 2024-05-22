#include "common.h"

char *shmem = nullptr;

void send(const Message *message)
{
    char *ptr = shmem;
    memcpy(ptr + 1, message, message->size);
    *ptr = 0x5;
}

const Message *recv(RingBuffer *ring_buffer)
{
    while (true) {
        sem_wait(&(ring_buffer->sem));  // 进入临界区

        const Message* received_message = ring_buffer_dequeue(ring_buffer);
        if (received_message) {
            if (crc32(received_message) == received_message->checksum) {  // 校验消息
                std::cout << "Bob received a valid message from Alice." << std::endl;
                printf("Bob received a valid message from Alice.");
                // 处理消息并生成新的消息
                Message* message2 = new Message(*received_message);  // 深拷贝消息
                increment_first_byte(message2);  // 修改消息体
                calculate_checksum(message2);  // 计算新的校验和

                // 将新的消息放入环形缓冲区
                if (!ring_buffer_enqueue(ring_buffer, message2)) {
                    std::cerr << "Failed to enqueue message." << std::endl;
                    delete message2;
                }

                delete received_message;  // 释放已处理的消息
            }
        }

        sem_post(&(ring_buffer->sem));  // 离开临界区
    }
}


int main() {
    // 打开共享内存对象
    int shm_fd = shm_open("/ipc_shm", O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");  // 打印错误信息
        return 1;
    }

    size_t size = sizeof(RingBuffer);
    // 将共享内存映射到进程的地址空间
    RingBuffer* ring_buffer = static_cast<RingBuffer*>(mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    if (ring_buffer == MAP_FAILED) {
        perror("mmap");  // 打印错误信息
        close(shm_fd);
        return 1;
    }

    recv(ring_buffer);

    // 解除共享内存映射
    if (munmap(ring_buffer, size) == -1) {
        perror("munmap");  // 打印错误信息
        close(shm_fd);
        return 1;
    }

    close(shm_fd);  // 关闭共享内存文件描述符

    return 0;
}
