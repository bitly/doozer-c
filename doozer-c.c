#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <json/json.h>
#include <time.h>
#include <utlist.h>
#include <arpa/inet.h>
#include <event.h>
#include <buffered_socket/buffered_socket.h>
#include "msg.pb-c.h"
#include "doozer-c.h"

#ifdef DEBUG
#define _DEBUG(...) fprintf(stdout, __VA_ARGS__)
#else
#define _DEBUG(...) do {;} while (0)
#endif

static void doozer_connectcb(struct BufferedSocket *buffsock, void *arg);
static void doozer_closecb(struct BufferedSocket *buffsock, void *arg);
static void doozer_readcb(struct BufferedSocket *buffsock, uint8_t *data, size_t len, void *arg);
static void doozer_writecb(struct BufferedSocket *buffsock, void *arg);
static void doozer_errorcb(struct BufferedSocket *buffsock, void *arg);
static void doozer_set_client_state(struct DoozerClient *client, int new_state);
int parse_endpoint(const char *input, size_t input_len, char **address, int *port);

struct DoozerClient *new_doozer_client(struct json_object *endpoint_list)
{
    struct DoozerClient *client;
    struct DoozerInstance *instance;
    char *endpoint_url;
    char *address;
    int port;
    int i;
    
    client = malloc(sizeof(struct DoozerClient));
    client->read_buffer = evbuffer_new();
    client->instances = NULL;
    client->state_callback = NULL;
    client->instance_count = 0;
    client->state = DOOZER_CLIENT_INIT;
    for (i = 0; i < json_object_array_length(endpoint_list); i++) {
        endpoint_url = (char *)json_object_get_string(json_object_array_get_idx(endpoint_list, i));
        if (!parse_endpoint(endpoint_url, strlen(endpoint_url), &address, &port)) {
            _DEBUG("%s: failed to parse endpoint %s\n", __FUNCTION__, endpoint_url);
            free_doozer_client(client);
            return NULL;
        }
        instance = new_doozer_instance(address, port);
        instance->client = client;
        client->instance_count++;
        LL_APPEND(client->instances, instance);
        free(address);
    }
    
    return client;
}

void free_doozer_client(struct DoozerClient *client)
{
    struct DoozerInstance *instance, *tmp;
    
    if (client) {
        LL_FOREACH_SAFE(client->instances, instance, tmp) {
            free_doozer_instance(instance);
        }
        evbuffer_free(client->read_buffer);
        free(client);
    }
}

struct DoozerInstance *new_doozer_instance(const char *address, int port)
{
    struct DoozerInstance *instance;
    
    instance = malloc(sizeof(struct DoozerInstance));
    instance->tag = 0;
    instance->transactions = NULL;
    instance->next = NULL;
    time(&instance->error_ts);
    instance->conn = new_buffered_socket(address, port, 
        doozer_connectcb, doozer_closecb, 
        doozer_readcb, doozer_writecb, doozer_errorcb, 
        instance);
    
    return instance;
}

void free_doozer_instance(struct DoozerInstance *doozerd)
{
    if (doozerd) {
        free_buffered_socket(doozerd->conn);
        free(doozerd);
    }
}

int parse_endpoint(const char *input, size_t input_len, char **address, int *port)
{
    // parse from <address>:<port>
    char *tmp = NULL;
    char *tmp_port = NULL;
    char *tmp_pointer;
    size_t address_len;
    
    // 0:0
    if (input_len < 3) {
        return 0;
    }
    
    tmp_pointer = (char *)strchr(input, ':');
    address_len = tmp_pointer - input;
    tmp_port = (char *)input + address_len + 1;
    *port = atoi(tmp_port);
    
    tmp = malloc(address_len + 1);
    memcpy(tmp, input, address_len);
    tmp[address_len] = '\0';
    *address = tmp;
    
    return 1;
}

void doozer_client_connect(struct DoozerClient *client, void (*state_callback)(struct DoozerClient *client))
{
    struct DoozerInstance *instance;
    
    if ((client->state == DOOZER_CLIENT_CONNECTED) || (client->state == DOOZER_CLIENT_CONNECTING)) {
        return;
    }
    
    client->state = DOOZER_CLIENT_CONNECTING;
    client->state_callback = state_callback;
    LL_FOREACH(client->instances, instance) {
        doozer_instance_connect(instance);
    }
}

int doozer_instance_connect(struct DoozerInstance *instance)
{
    return buffered_socket_connect(instance->conn);
}

void doozer_instance_reconnect(struct DoozerInstance *instance)
{
    time_t now;
    
    if (instance->conn->state == BS_DISCONNECTED) {
        time(&now);
        _DEBUG("%s: instance %p reconnect in %ld secs\n", __FUNCTION__, instance, 
               DOOZER_RECONNECT_DELAY - (now - instance->error_ts));
        if ((now - instance->error_ts) >= DOOZER_RECONNECT_DELAY) {
            time(&instance->error_ts); // reset the error timestamp
            doozer_instance_connect(instance);
        }
    }
}

struct DoozerInstance *doozer_get_instance(struct DoozerClient *client)
{
    static int index = 0;
    struct DoozerInstance *instance, *chosen_instance = NULL;
    int i = 0;
    
    if (!client->instance_count) {
        return NULL;
    }
    
    // round robin
    LL_FOREACH(client->instances, instance) {
        if (i == index) {
            if (instance->conn->state == BS_CONNECTED) {
                chosen_instance = instance;
                break;
            }
            
            _DEBUG("%s: instance %p is disconnected, attempting reconnect\n", __FUNCTION__, instance);
            doozer_instance_reconnect(instance);
        }
        i++;
    }
    
    index = (index + 1) % client->instance_count;
    
    return chosen_instance;
}

size_t doozer_instance_write(struct DoozerInstance *instance, void *data, size_t len)
{
    return buffered_socket_write(instance->conn, data, len);
}

struct DoozerTransaction *new_doozer_transaction(int verb, 
    const char *path, size_t path_len, 
    uint8_t *value, size_t value_len, int64_t rev, 
    void (*callback)(struct DoozerTransaction *transaction, void *arg), void *cbarg)
{
    struct DoozerTransaction *transaction;
    
    assert(path != NULL);
    assert(path_len > 0);
    
    transaction = calloc(1, sizeof(struct DoozerTransaction));
    transaction->callback = callback;
    transaction->cbarg = cbarg;
    transaction->pb_resp = NULL;
    doozer__request__init(&transaction->pb_req);
    
    transaction->pb_req.path = calloc(1, path_len + 1);
    memcpy(transaction->pb_req.path, path, path_len);
    
    transaction->pb_req.has_verb = 1;
    transaction->pb_req.verb = verb;
    
    if (value && value_len) {
        transaction->pb_req.has_value = 1;
        transaction->pb_req.value.len = value_len;
        transaction->pb_req.value.data = malloc(value_len);
        memcpy(transaction->pb_req.value.data, value, value_len);
    }
    
    if (rev != -1) {
        transaction->pb_req.has_rev = 1;
        transaction->pb_req.rev = rev;
    }
    
    return transaction;
}

void free_doozer_transaction(struct DoozerTransaction *transaction)
{
    if (transaction) {
        free(transaction->pb_req.path);
        if (transaction->pb_req.has_value) {
            free(transaction->pb_req.value.data);
        }
        if (transaction->pb_resp) {
            doozer__response__free_unpacked(transaction->pb_resp, NULL);
        }
        free(transaction);
    }
}

char *doozer_pack_transaction(struct DoozerTransaction *transaction, size_t *len)
{
    void *buf;
    uint32_t len_be;
    
    *len = doozer__request__get_packed_size(&transaction->pb_req);
    buf = malloc(*len + sizeof(uint32_t));
    doozer__request__pack(&transaction->pb_req, buf + sizeof(uint32_t));
    
    _DEBUG("%s: %lu serialized bytes\n", __FUNCTION__, *len);
    
    // prepend big-endian length of the packed msg
    len_be = htonl(*len);
    memcpy(buf, &len_be, sizeof(uint32_t));
    *len += sizeof(uint32_t);
    
    return buf;
}

int doozer_send(struct DoozerClient *client, struct DoozerTransaction *transaction)
{
    struct DoozerInstance *instance;
    void *buf;
    size_t len;
    
    instance = doozer_get_instance(client);
    if (!instance) {
        // call the callback (transaction->pb_resp == NULL)
        (*transaction->callback)(transaction, transaction->cbarg);
        return RET_DOOZER_INSTANCE_UNAVAILABLE;
    }
    
    transaction->instance = instance;
    // generate a unique tag for this transaction
    transaction->pb_req.has_tag = 1;
    transaction->pb_req.tag = instance->tag++;
    _DEBUG("%s: adding transaction %p to %p\n", __FUNCTION__, transaction, instance->transactions);
    DL_APPEND(instance->transactions, transaction);
    buf = doozer_pack_transaction(transaction, &len);
    doozer_instance_write(instance, buf, len);
    free(buf);
    
    return RET_DOOZER_OK;
}

struct DoozerTransaction *doozer_set(const char *path, size_t path_len, 
    uint8_t *value, size_t value_len, int64_t rev, 
    void (*callback)(struct DoozerTransaction *transaction, void *arg), void *cbarg)
{
    return new_doozer_transaction(DOOZER__REQUEST__VERB__SET, 
        path, path_len, value, value_len, rev, callback, cbarg);
}

struct DoozerTransaction *doozer_get(const char *path, size_t path_len, int64_t rev, 
    void (*callback)(struct DoozerTransaction *transaction, void *arg), void *cbarg)
{
    return new_doozer_transaction(DOOZER__REQUEST__VERB__GET, 
        path, path_len, NULL, 0, rev, callback, cbarg);
}

struct DoozerTransaction *doozer_del(const char *path, size_t path_len, int64_t rev, 
    void (*callback)(struct DoozerTransaction *transaction, void *arg), void *cbarg)
{
    return new_doozer_transaction(DOOZER__REQUEST__VERB__DEL, 
        path, path_len, NULL, 0, rev, callback, cbarg);
}

static void set_state_and_callback(struct DoozerClient *client)
{
    struct DoozerInstance *instance;
    int instances_still_connecting = 0;
    int instances_connected = 0;
    
    LL_FOREACH(client->instances, instance) {
        if (instance->conn->state < BS_CONNECTED) {
            instances_still_connecting++;
        }
        if (instance->conn->state == BS_CONNECTED) {
            instances_connected++;
        }
    }
    
    _DEBUG("%s: %d connecting, %d connected\n", __FUNCTION__, instances_still_connecting, instances_connected);
    
    if (!instances_still_connecting) {
        doozer_set_client_state(client, instances_connected ? DOOZER_CLIENT_CONNECTED : DOOZER_CLIENT_DISCONNECTED);
    }
}

static void doozer_set_client_state(struct DoozerClient *client, int new_state)
{
    int current_state;
    
    _DEBUG("%s: START client->state = %d\n", __FUNCTION__, client->state);
    
    current_state = client->state;
    client->state = new_state;
    
    _DEBUG("%s: END client->state = %d\n", __FUNCTION__, client->state);
    
    // we want to call our state callback only when it changes
    if (current_state != new_state) {
        (*client->state_callback)(client);
    }
}

static void doozer_connectcb(struct BufferedSocket *buffsock, void *arg)
{
    struct DoozerInstance *instance = (struct DoozerInstance *)arg;
    struct DoozerClient *client = instance->client;
    
    _DEBUG("%s: %p\n", __FUNCTION__, instance);
    
    set_state_and_callback(client);
}

static void doozer_closecb(struct BufferedSocket *buffsock, void *arg)
{
    struct DoozerInstance *instance = (struct DoozerInstance *)arg;
    struct DoozerClient *client = instance->client;
    
    _DEBUG("%s: %p\n", __FUNCTION__, instance);
    
    set_state_and_callback(client);
}

static void doozer_readcb(struct BufferedSocket *buffsock, uint8_t *data, size_t len, void *arg)
{
    struct DoozerInstance *instance = (struct DoozerInstance *)arg;
    struct DoozerClient *client = instance->client;
    Doozer__Response *resp;
    struct DoozerTransaction *transaction;
    static int current_state = DOOZER_READ_MSG_SIZE;
    static size_t msg_size;
    uint32_t msg_size_be;
    
    if (len) {
        evbuffer_add(client->read_buffer, data, len);
    }
    
    while (EVBUFFER_LENGTH(client->read_buffer) >= 4) {
        _DEBUG("%s: %lu bytes read (in buffer %lu)\n", __FUNCTION__, len, EVBUFFER_LENGTH(client->read_buffer));
        
        if ((current_state == DOOZER_READ_MSG_SIZE) && (EVBUFFER_LENGTH(client->read_buffer) >= 4)) {
            memcpy(&msg_size_be, EVBUFFER_DATA(client->read_buffer), 4);
            evbuffer_drain(client->read_buffer, 4);
            // convert message length header from big-endian 
            msg_size = ntohl(msg_size_be);
            _DEBUG("%s: msg_size = %lu bytes \n", __FUNCTION__, msg_size);
            current_state = DOOZER_READ_MSG_BODY;
        }
        
        if ((current_state == DOOZER_READ_MSG_BODY) && (EVBUFFER_LENGTH(client->read_buffer) >= msg_size)) {
            resp = doozer__response__unpack(NULL, msg_size, (uint8_t *)EVBUFFER_DATA(client->read_buffer));
            evbuffer_drain(client->read_buffer, msg_size);
            
            assert(resp != NULL);
            
            // find the transaction by tag
            DL_FOREACH(instance->transactions, transaction) {
                if (transaction->pb_req.tag == resp->tag) {
                    break;
                }
            }
            
            assert(transaction != NULL);

            _DEBUG("%s: removing transaction %p from deque (head = %p) - instance %p\n", 
                   __FUNCTION__, transaction, instance->transactions, instance);
            
            DL_DELETE(instance->transactions, transaction);
            
            transaction->pb_resp = resp;
            if (transaction->pb_resp->has_err_code) {
                _DEBUG("%s: err_code: %d (%s)\n", __FUNCTION__, transaction->pb_resp->err_code, 
                       transaction->pb_resp->err_detail ? transaction->pb_resp->err_detail : "");
            }
            if (transaction->pb_resp->path) {
                _DEBUG("%s: path: %s\n", __FUNCTION__, transaction->pb_resp->path);
            }
            if (transaction->pb_resp->has_rev) {
                _DEBUG("%s: rev: %"PRIu64"\n", __FUNCTION__, transaction->pb_resp->rev);
            }
            if (transaction->pb_resp->has_value) {
                _DEBUG("%s: value.len: %lu\n", __FUNCTION__, transaction->pb_resp->value.len);
            }
            
            (*transaction->callback)(transaction, transaction->cbarg);
            
            // reset read state
            current_state = DOOZER_READ_MSG_SIZE;
        }
    }
}

static void doozer_writecb(struct BufferedSocket *buffsock, void *arg)
{
    struct DoozerInstance *instance = (struct DoozerInstance *)arg;
    
    _DEBUG("%s: %p\n", __FUNCTION__, instance);
}

static void doozer_errorcb(struct BufferedSocket *buffsock, void *arg)
{
    struct DoozerInstance *instance = (struct DoozerInstance *)arg;
    struct DoozerTransaction *transaction, *tmp_transaction;
    
    // flush the transactions for this instance
    DL_FOREACH_SAFE(instance->transactions, transaction, tmp_transaction) {
        // call the callback (transaction->pb_resp == NULL)
        (*transaction->callback)(transaction, transaction->cbarg);
        DL_DELETE(instance->transactions, transaction);
    }
    
    _DEBUG("%s: %p\n", __FUNCTION__, instance);
    
    time(&instance->error_ts);
}
