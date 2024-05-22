#include "common.h"

char *shmem = nullptr;

void send(const Message *message)
{
    char *ptr = shmem;
    memcpy(ptr + 1, message, message->size);
    *ptr = 0x5;
}

const Message *recv()
{
    static Message *m = (Message *)malloc(MESSAGE_SIZES[4]);
    while(1) {
      char *ptr = shmem;
      if (0xA == *ptr) {
          memcpy(m, ptr + 1, sizeof(Message));
          char *ptr1 = ptr + 1 + sizeof(Message);
          memcpy(m->payload, ptr1, m->payload_size());
          return m;
      }
    }
}


int main() {
    int shm_fd = shm_open("/ipc_shm", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return 1;
    }

    size_t size = sizeof(RingBuffer);
    if (ftruncate(shm_fd, size) == -1) {
        perror("ftruncate");
        close(shm_fd);
        return 1;
    }

    RingBuffer* ring_buffer = static_cast<RingBuffer*>(mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    if (ring_buffer == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return 1;
    }

    ring_buffer->head = 0;
    ring_buffer->tail = 0;
    sem_init(&(ring_buffer->sem), 1, 1);

    while (true) {
        // Step 3: Bob 收到 m1 后，检查 m1 的消息体与其消息头内的校验和是否一致
        const Message* received_message = nullptr;
        while (true) {
            sem_wait(&(ring_buffer->sem));
            if (ring_buffer->head != ring_buffer->tail) {
                received_message = ring_buffer->data[ring_buffer->head];
                if (received_message == nullptr) {
                    std::cout << "Bob received null message from Alice."<< std::endl;
                    break;
                }
                if (crc32(received_message) == received_message->checksum) {
                    std::cout << "Bob received a valid message from Alice." << std::endl;
                //     ring_buffer->head = (ring_buffer->head + 1) % RING_SIZE;
                //     sem_post(&(ring_buffer->sem));
                //     break;
                }
            }
            sem_post(&(ring_buffer->sem));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 等待一段时间后重试
        }

        // Step 4: Bob 将 m1 复制为 m2，将 m2 的消息体的第一个字节自增1
        Message* message2 = new Message(*received_message);
        increment_first_byte(message2);

        // Step 5: Bob 计算 m2 的消息体的校验和记在 m2 的消息头内
        calculate_checksum(message2);

        // Step 6: Bob 将 m2 发送给 Alice
        sem_wait(&(ring_buffer->sem));
        ring_buffer->data[ring_buffer->tail] = message2;
        ring_buffer->tail = (ring_buffer->tail + 1) % RING_SIZE;
        sem_post(&(ring_buffer->sem));  // 释放信号量，通知 Alice 可以处理消息
    }

    if (munmap(ring_buffer, size) == -1) {
        perror("munmap");
        close(shm_fd);
        return 1;
    }

    close(shm_fd);

    if (shm_unlink("/ipc_shm") == -1) {
        perror("shm_unlink");
        return 1;
    }

    return 0;
}
