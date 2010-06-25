#include <config/config.h>
#include <config/db.h>
#include <stdlib.h>
#include <dbg.h>
#include <adt/list.h>
#include <server.h>
#include <string.h>
#include <stdarg.h>
#include <sqlite3.h>


#define arity(N) check(cols == (N), "Wrong number of cols: %d but expect %d", cols, (N)); int max = N;
#define SQL(Q, ...) sqlite3_mprintf((Q), ##__VA_ARGS__)
#define SQL_FREE(Q) if(Q) sqlite3_free(Q)

int Config_handler_load_cb(void *param, int cols, char **data, char **names)
{
    arity(5);
    Handler **target = (Handler **)param;

    debug("Making a handler for %s:%s:%s:%s:%s", data[0], data[1], data[2],
            data[3], data[4]);

    *target = Handler_create(data[1], data[2], data[3], data[4]);

    check(*target, "Failed to create handler for %s:%s:%s:%s:%s", 
            data[0], data[1], data[2], data[3], data[4]);

    return 0;
error:
    return -1;
}

int Config_proxy_load_cb(void *param, int cols, char **data, char **names)
{
    arity(3);
    Proxy **target = (Proxy **)param;

    debug("Making a proxy for %s:%s:%s", data[0], data[1], data[2]);

    *target = Proxy_create(data[2], atoi(data[1]));
    check(*target, "Failed to create proxy for %s:%s:%s", data[0], data[1], data[2]);

    return 0;
error:
    return -1;
}

int Config_route_load_cb(void *param, int cols, char **data, char **names)
{
    arity(4);
    
    Host *host = (Host *)param;
    check(host, "Should get a host on callback for routes.");

    char *query = NULL;
    int rc = 0;
    void *target = NULL;
    BackendType type = 0;
    const char *HANDLER_QUERY = "SELECT id, send_spec, send_ident, recv_spec, recv_ident FROM handler WHERE id=%s";
    const char *PROXY_QUERY = "SELECT id, addr, port FROM proxy where id=%s";


    if(strcmp(data[3], "handler") == 0) {
        query = SQL(HANDLER_QUERY, data[2]);

        rc = DB_exec(query, Config_handler_load_cb, &target);
        check(rc == 0, "Failed to find handler for route: %s (id: %s)",
                data[1], data[0]);

        type = BACKEND_HANDLER;

    } else if(strcmp(data[3], "proxy") == 0) {
        query = SQL(PROXY_QUERY, data[2]);
        rc = DB_exec(query, Config_proxy_load_cb, &target);
        check(rc == 0, "Failed to find handler for route: %s (id: %s)",
                data[1], data[0]);

        type = BACKEND_PROXY;

    } else {
        sentinel("Invalid handler type: %s for host: %s", data[3], host->name);
    }

    Host_add_backend(host, data[1], strlen(data[1]), type, target);

    SQL_FREE(query);

    return 0;
error:

    SQL_FREE(query);
    return -1;
}

int Config_host_load_cb(void *param, int cols, char **data, char **names)
{
    arity(3);

    Server *srv = (Server *)param;
    const char *ROUTE_QUERY = "SELECT id, path, target_id, target_type FROM route WHERE host_id=%s";
    check(srv, "Should be given the server to add hosts to.");

    Host *host = Host_create(data[1]);
    check(host, "Failed to create host: %s (id: %d)", data[1], data[0]);

    char *query = SQL(ROUTE_QUERY, data[0]);

    DB_exec(query, Config_route_load_cb, host);

    debug("Adding host %s (id: %d) to server at pattern %s", data[1], data[0], data[2]);


    Server_add_host(srv, data[2], strlen(data[2]), host);

    if(srv->default_host == NULL) Server_set_default_host(srv, host);

    SQL_FREE(query);
    return 0;
error:

    SQL_FREE(query);
    return -1;
}

int Config_server_load_cb(void *param, int cols, char **data, char **names)
{
    arity(4);

    list_t *servers = (list_t *)param;
    Server *srv = NULL;
    const char *HOST_QUERY = "SELECT id, name, matching FROM host where server_id = %s";
 
    debug("Configuring server ID: %s, default host: %s, port: %s", 
            data[1], data[2], data[3]);

    srv = Server_create(data[3]);
    check(srv, "Failed to create server %s on port %s", data[2], data[3]);

    char *query = SQL(HOST_QUERY, data[0]);
    check(query, "Failed to craft query.");

    int rc = DB_exec(query, Config_host_load_cb, srv);
    check(rc == 0, "Failed to find hosts for server %s on port %s (id: %s)",
            data[2], data[3], data[0]);

    list_append(servers, lnode_create(srv));

    SQL_FREE(query);
    return 0;

error:
    SQL_FREE(query);
    return -1;
}


list_t *Config_load(const char *path)
{
    list_t *servers = list_create(LISTCOUNT_T_MAX);
    const char *SERVER_QUERY = "SELECT id, uuid, default_host, port FROM server";

    int rc = DB_init(path);
    check(rc == 0, "Failed to load config database: %s", path);

    rc = DB_exec(SERVER_QUERY, Config_server_load_cb, servers);
    check(rc == 0, "Failed to select servers from: %s", path);


    return servers;
error:

    return NULL;
}
