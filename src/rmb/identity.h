#ifndef IDENTITYH
#define IDENTITYH

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "utils.h"

typedef struct _server server;

char *show_servers(char *server_ip, u_short server_port);
list *parse_servers(char *id_serv_info);

/* Server Functions */
char *get_name(server *this);
char *get_ip_address(server *this);
u_short get_udp_port(server *this);
u_short get_tpc_port(server *this);

void free_server(item got_item);
void print_server(item got_item);

#endif
