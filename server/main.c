#include <android/looper.h>
#include <android/sensor.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define LOOPER_ID 3
#define MAX_CONNECTION 10

typedef struct __attribute__((packed)) {
  float x, y, z;
} pkt_accel_t;

static volatile int running = 1;
static int server_fd;
static int clients_fd[MAX_CONNECTION];
static pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;

static volatile int looper_init_ok = 1;
static pthread_cond_t looper_cv = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t looper_mtx = PTHREAD_MUTEX_INITIALIZER;

static ASensorManager *sensorManager = NULL;

struct {
  const ASensor *accelerometer;
} static sensors = {.accelerometer = NULL};

static ALooper *looper = NULL;
static ASensorEventQueue *sensorEventQueue = NULL;

static void cleanup_sensors(void) {
  if (sensors.accelerometer) {
    ASensorEventQueue_disableSensor(sensorEventQueue, sensors.accelerometer);
    sensors.accelerometer = NULL;
  }
}

static inline bool is_valid_fd(int fd) { return fd >= 0; }

static int find_free_slot(void) {
  for (int i = 0; i < MAX_CONNECTION; i++)
    if (clients_fd[i] == -1) return i;
  return -1;
}

static void remove_client(int i) {
  if (is_valid_fd(clients_fd[i])) {
    close(clients_fd[i]);
    clients_fd[i] = -1;
  }
}

static void handle_sigint(int sig) {
  (void)sig;
  running = 0;
  if (looper) ALooper_wake(looper);

  pthread_mutex_lock(&clients_lock);
  for (int i = 0; i < MAX_CONNECTION; i++) remove_client(i);
  pthread_mutex_unlock(&clients_lock);

  if (is_valid_fd(server_fd)) close(server_fd);

  // printf("\nClean exit...\n");
  // fflush(stdout);
}

static int get_sensor_events(int fd, int events, void *data) {
  (void)fd;
  (void)events;
  (void)data;

  // printf("Entering: get_sensor_events\n");
  ASensorEvent event;
  while (ASensorEventQueue_getEvents(sensorEventQueue, &event, 1) > 0) {
    if (event.type != ASENSOR_TYPE_ACCELEROMETER) continue;

    pkt_accel_t accel_pkt = {
        .x = event.acceleration.x, .y = event.acceleration.y, .z = event.acceleration.z};

    // printf("Accel: x=%f y=%f z=%f\n", accel_pkt.x, accel_pkt.y, accel_pkt.z);

    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CONNECTION; i++) {
      int cfd = clients_fd[i];
      if (!is_valid_fd(cfd)) continue;

      ssize_t n = send(cfd, &accel_pkt, sizeof(accel_pkt), MSG_NOSIGNAL);
      if (n <= 0) {
        remove_client(i);
        printf("Client disconnected (%d)\n", cfd);
        fflush(stdout);
      }
    }
    pthread_mutex_unlock(&clients_lock);
  }

  // printf("Leaving: get_sensor_events\n");
  return 1;
}

void *sensor_thread_fn(void *arg) {
  (void)arg;
  pthread_mutex_lock(&looper_mtx);

  looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
  if (!looper) {
    looper_init_ok = 0;
    pthread_cond_signal(&looper_cv);
    pthread_mutex_unlock(&looper_mtx);
    NULL;
  }

  pthread_cond_signal(&looper_cv);
  pthread_mutex_unlock(&looper_mtx);

  while (running) {
    ALooper_pollOnce(-1, NULL, NULL, NULL);
  }

  printf("Performing sensors cleanup\n");
  cleanup_sensors();
  ASensorManager_destroyEventQueue(sensorManager, sensorEventQueue);
  printf("Cleanup done\n");

  return NULL;
}

void *net_thread_fn(void *arg) {
  (void)arg;

  struct pollfd pfd = {.fd = server_fd, .events = POLLIN};

  while (running) {
    int ret = poll(&pfd, 1, 1000);
    if (ret < 0) {
      if (errno == EINTR) continue;
      perror("poll");
      break;
    }

    if (ret == 0) continue;

    if (pfd.revents & POLLIN) {
      int fd = accept(server_fd, NULL, NULL);
      if (fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) perror("accept");
        continue;
      }

      pthread_mutex_lock(&clients_lock);
      int slot = find_free_slot();
      if (slot != -1) {
        printf("Client connected: (%d)\n", fd);
        fflush(stdout);
        clients_fd[slot] = fd;
      } else {
        close(fd);
      }
      pthread_mutex_unlock(&clients_lock);
    }
  }

  return NULL;
}

int main(void) {
  signal(SIGINT, handle_sigint);

  pthread_t sensor_thread;
  pthread_create(&sensor_thread, NULL, sensor_thread_fn, NULL);

  pthread_mutex_lock(&looper_mtx);
  while (!looper && looper_init_ok) pthread_cond_wait(&looper_cv, &looper_mtx);
  pthread_mutex_unlock(&looper_mtx);

  if (!looper || !looper_init_ok) {
    fprintf(stderr, "Failed to prepare ALooper*\n");
    return 1;
  }

  for (int i = 0; i < MAX_CONNECTION; i++) clients_fd[i] = -1;

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_in addr = {
      .sin_family = AF_INET, .sin_port = htons(PORT), .sin_addr.s_addr = INADDR_ANY};

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    perror("bind");
    return 1;
  }

  if (listen(server_fd, 5) != 0) {
    perror("listen");
    return 1;
  }

  int flags = fcntl(server_fd, F_GETFL, 0);
  fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

  sensorManager = ASensorManager_getInstanceForPackage(NULL);
  if (!sensorManager) {
    fprintf(stderr, "Failed to get sensor manager\n");
    return 1;
  }

  sensors.accelerometer =
      ASensorManager_getDefaultSensor(sensorManager, ASENSOR_TYPE_ACCELEROMETER);
  if (!sensors.accelerometer) {
    fprintf(stderr, "Could not get ASENSOR_TYPE_ACCELEROMETER\n");
    return 1;
  }

  // printf("Using sensor: %s\n", ASensor_getName(sensors.accelerometer));

  sensorEventQueue =
      ASensorManager_createEventQueue(sensorManager, looper, LOOPER_ID, get_sensor_events, NULL);
  if (!sensorEventQueue) {
    fprintf(stderr, "Failed to create event queue\n");
    return 1;
  }

  if (ASensorEventQueue_enableSensor(sensorEventQueue, sensors.accelerometer) < 0) {
    fprintf(stderr, "Failed to enable accelerometer\n");
    return 1;
  }

  ASensorEventQueue_setEventRate(sensorEventQueue, sensors.accelerometer,
                                 20000);  // 50Hz

  pthread_t net_thread;
  pthread_create(&net_thread, NULL, net_thread_fn, NULL);

  printf("Server running on port %d (Ctrl+C to stop)\n", PORT);

  pthread_join(sensor_thread, NULL);
  pthread_join(net_thread, NULL);

  fflush(stdout);
  return 0;
}
