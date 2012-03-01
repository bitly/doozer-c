#ifndef __doozer_h
#define __doozer_h

#include <inttypes.h>
#include <time.h>
#include "msg.pb-c.h"

#define DOOZER_RECONNECT_DELAY 60

enum DoozerReadState {
    DOOZER_READ_MSG_SIZE, 
    DOOZER_READ_MSG_BODY
};

enum DoozerClientState {
    DOOZER_CLIENT_INIT, 
    DOOZER_CLIENT_CONNECTING, 
    DOOZER_CLIENT_CONNECTED, 
    DOOZER_CLIENT_DISCONNECTED
};

enum DoozerReturnCode {
    RET_DOOZER_OK,
    RET_DOOZER_INSTANCE_UNAVAILABLE,
};

struct DoozerTransaction {
    struct DoozerInstance *instance;
    Doozer__Request pb_req;
    Doozer__Response *pb_resp;
    void (*callback)(struct DoozerTransaction *transaction, void *arg);
    void *cbarg;
    struct DoozerTransaction *prev;
    struct DoozerTransaction *next;
};

struct DoozerInstance {
    struct DoozerClient *client;
    struct BufferedSocket *conn;
    struct DoozerTransaction *transactions; // deque
    uint32_t tag;
    time_t error_ts;
    struct DoozerInstance *next;
};

struct DoozerClient {
    struct DoozerInstance *instances;
    struct evbuffer *read_buffer;
    int instance_count;
    int state;
    void (*state_callback)(struct DoozerClient *client);
};

struct DoozerInstance *new_doozer_instance(const char *address, int port);
void free_doozer_instance(struct DoozerInstance *doozerd);
struct DoozerClient *new_doozer_client(struct json_object *endpoint_list);
void free_doozer_client(struct DoozerClient *client);
void doozer_client_connect(struct DoozerClient *client, void (*state_callback)(struct DoozerClient *client));
int doozer_instance_connect(struct DoozerInstance *instance);
void doozer_instance_reconnect(struct DoozerInstance *instance);
struct DoozerInstance *doozer_get_instance(struct DoozerClient *client);
size_t doozer_instance_write(struct DoozerInstance *instance, void *data, size_t len);
struct DoozerTransaction *new_doozer_transaction(int verb, 
    const char *path, size_t path_len, 
    uint8_t *value, size_t value_len, int64_t rev, 
    void (*callback)(struct DoozerTransaction *transaction, void *arg), void *cbarg);
void free_doozer_transaction(struct DoozerTransaction *transaction);
char *doozer_pack_transaction(struct DoozerTransaction *transaction, size_t *len);
int doozer_send(struct DoozerClient *client, struct DoozerTransaction *transaction);
struct DoozerTransaction *doozer_set(const char *path, size_t path_len, 
    uint8_t *value, size_t value_len, int64_t rev, 
    void (*callback)(struct DoozerTransaction *transaction, void *arg), void *cbarg);
struct DoozerTransaction *doozer_get(const char *path, size_t path_len, int64_t rev, 
    void (*callback)(struct DoozerTransaction *transaction, void *arg), void *cbarg);
struct DoozerTransaction *doozer_del(const char *path, size_t path_len, int64_t rev, 
    void (*callback)(struct DoozerTransaction *transaction, void *arg), void *cbarg);
struct DoozerTransaction *doozer_stat(const char *path, size_t path_len, int64_t rev, 
    void (*callback)(struct DoozerTransaction *transaction, void *arg), void *cbarg);

#endif
