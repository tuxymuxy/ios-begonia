/*
 * bluetool_shim: Stand-in for Apple's BlueTool daemon on com.apple.BlueTool.
 * Answers RunBuiltinScript boot, init, and deepsleep with result = 0
 * so bluetoothd's asynchronous power initialization succeeds immediately.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <dispatch/dispatch.h>

typedef void * xpc_object_t;
typedef void * xpc_connection_t;
typedef void * xpc_type_t;
typedef void (^xpc_handler_t)(xpc_object_t object);

extern const struct _xpc_type_s _xpc_type_dictionary;
extern const struct _xpc_type_s _xpc_type_connection;

#define XPC_TYPE_DICTIONARY (&_xpc_type_dictionary)
#define XPC_TYPE_CONNECTION (&_xpc_type_connection)
#define XPC_CONNECTION_MACH_SERVICE_LISTENER (1ULL << 0)

xpc_connection_t xpc_connection_create_mach_service(const char *name, dispatch_queue_t targetq, uint64_t flags);
void xpc_connection_set_event_handler(xpc_connection_t connection, xpc_handler_t handler);
void xpc_connection_resume(xpc_connection_t connection);
xpc_type_t xpc_get_type(xpc_object_t object);
const char *xpc_dictionary_get_string(xpc_object_t xdict, const char *key);
xpc_object_t xpc_dictionary_create_reply(xpc_object_t original);
void xpc_dictionary_set_int64(xpc_object_t xdict, const char *key, int64_t value);
void xpc_connection_send_message(xpc_connection_t connection, xpc_object_t message);

static void peer_handler(xpc_connection_t peer, xpc_object_t event)
{
    if (xpc_get_type(event) == XPC_TYPE_DICTIONARY) {
        const char *script = xpc_dictionary_get_string(event, "RunBuiltinScript");
        printf("bluetool_shim: RunBuiltinScript %s -> result 0\n", script ? script : "(null)");
        xpc_object_t reply = xpc_dictionary_create_reply(event);
        if (reply) {
            xpc_dictionary_set_int64(reply, "result", 0);
            xpc_connection_send_message(peer, reply);
        }
    }
}

int main(int argc, char *argv[])
{
    printf("bluetool_shim: Active on com.apple.BlueTool\n");
    xpc_connection_t service = xpc_connection_create_mach_service(
        "com.apple.BlueTool", NULL, XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if (!service) {
        printf("bluetool_shim: Failed to create mach service\n");
        return 1;
    }
    xpc_connection_set_event_handler(service, ^(xpc_object_t conn) {
        if (xpc_get_type(conn) == XPC_TYPE_CONNECTION) {
            xpc_connection_set_event_handler(conn, ^(xpc_object_t event) {
                peer_handler(conn, event);
            });
            xpc_connection_resume(conn);
        }
    });
    xpc_connection_resume(service);
    dispatch_main();
    return 0;
}
