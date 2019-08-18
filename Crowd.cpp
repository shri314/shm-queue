#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

#define CHECK(ST)                                                               \
   do {                                                                         \
      int e = (ST);                                                             \
      switch (e) {                                                              \
         case 0:                                                                \
            break;                                                              \
         case EINVAL:                                                           \
            std::cout << #ST << " failed - EINVAL\n";                           \
            break;                                                              \
         default:                                                               \
            std::cout << #ST << " failed - unknown error - " << e << "\n";      \
            break;                                                              \
      }                                                                         \
   } while (false)



template <unsigned N>
struct blocking_queue_t {
private:
   static_assert(N > 0 && N < std::numeric_limits<decltype(N)>::max(), "N should be atleast 1");

   bool m_init;
   pthread_mutex_t m_mutex;
   pthread_cond_t m_cond_is_full;
   pthread_cond_t m_cond_is_empty;

   char m_buffer[N + 1];
   decltype(N) m_p;
   decltype(N) m_c;

private:
   static auto curr(auto pos) {
      return pos % (N + 1);
   }

   static auto prev(auto pos) {
      if (pos == 0)
         return N;
      else
         return (pos - 1) % (N + 1);
   }

   static auto next(auto pos) {
      return curr(pos + 1);
   }

public:
   void init() {
      if (!m_init) {

         {
            pthread_mutexattr_t attr;
            CHECK(pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED));
            CHECK(pthread_mutex_init(&m_mutex, &attr));
         }

         {
            pthread_condattr_t attr;
            CHECK(pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED));
            CHECK(pthread_cond_init(&m_cond_is_full, &attr));
         }

         {
            pthread_condattr_t attr;
            CHECK(pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED));
            CHECK(pthread_cond_init(&m_cond_is_empty, &attr));
         }

         m_p    = 1;
         m_c    = 0;
         m_init = true;
      }
   }

   auto is_empty() const {
      return size() == 0;
   }

   bool is_full() const {
      return size() == N - 1;
   }

   bool size() const {
      return curr(prev(m_p) - curr(m_c));
   }

   void push(char x) {
      CHECK(pthread_mutex_lock(&m_mutex));

      while (is_full()) {
         // std::cout << "is_full\n";
         pthread_cond_wait(&m_cond_is_full, &m_mutex);
      }

      {
         // push
         // std::cout << "push:: m_p = " << m_p << ", m_c = " << m_c << ", sz = " << size() << "\n";
         m_buffer[prev(m_p)] = x;

         m_p = next(m_p);
         // std::cout << "push:: m_p = " << m_p << ", m_c = " << m_c << ", sz = " << size() << "\n";
      }

      CHECK(pthread_cond_broadcast(&m_cond_is_empty));

      CHECK(pthread_mutex_unlock(&m_mutex));
   }

   char pop() {
      CHECK(pthread_mutex_lock(&m_mutex));

      while (is_empty()) {
         // std::cout << "is_empty\n";
         CHECK(pthread_cond_wait(&m_cond_is_empty, &m_mutex));
      }

      char ret;
      {
         // pop
         // std::cout << "pop:: m_p = " << m_p << ", m_c = " << m_c << ", sz = " << size() << "\n";
         ret = m_buffer[curr(m_c)];

         m_c = next(m_c);
         // std::cout << "pop:: m_p = " << m_p << ", m_c = " << m_c << ", sz = " << size() << "\n";
      }

      CHECK(pthread_cond_broadcast(&m_cond_is_full));

      CHECK(pthread_mutex_unlock(&m_mutex));

      return ret;
   }
};

int main(int argc, char** argv) {
   --argc;
   ++argv;

   if (sysconf(_SC_THREAD_PROCESS_SHARED) == -1) {
      std::cerr << "bad kernel implementation\n";
      exit(1);
   }

   const int is_server = argc > 0 ? false : true;

   int shm_fd = shm_open("/abc", O_CREAT | O_RDWR, S_IRWXU | S_IRWXG);
   if (shm_fd < 0) {
      perror("shm_open failed");
      exit(1);
   }

   using queue_t = blocking_queue_t<1024>;

   struct stat buf;
   if (fstat(shm_fd, &buf) == -1 ) {
      perror("Error on fstat\n");
      exit(-1);
   }

   bool server_init = false;
   if(buf.st_size != sizeof(queue_t)) {
      server_init = true;
      std::cout << "server needs init...\n";
   }

   if (ftruncate(shm_fd, sizeof(queue_t)) == -1) {
      perror("ftruncate failed");
      exit(-1);
   }

   void* memory = mmap(NULL, sizeof(queue_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
   if (memory == MAP_FAILED) {
      perror("mmap failed");
      exit(1);
   }

   if (is_server) {
      //std::cout << "SERVER (Ctrl-D to exit)\n";
      queue_t* queue = (queue_t*)memory;
      queue->init();

      std::string line;
      while (true) {
         //std::cout << "Push: " << std::flush;

         if (std::getline(std::cin, line)) {
            for (char x : line)
               queue->push(x);
            queue->push('\n');
         } else {
            //std::cout << "\n";
            queue->push('\0');
            break;
         }
      }
   } else {
      //std::cout << "CLIENT\n";
      queue_t* queue = (queue_t*)memory;
      queue->init();

      bool newline = true;
      while (true) {
         if(newline) {
            //std::cout << "Popped: " << std::flush;
            newline = false;
         }

         char x = queue->pop();
         if (x == '\0') {
            //std::cout << '\n';
            break;
         }

         std::cout << x << std::flush;

         if(x == '\n')
            newline = true;
      }
   }
}
