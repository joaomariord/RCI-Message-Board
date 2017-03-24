#include <time.h>
#include <errno.h>
#include <sys/timerfd.h>

#include "message.h"
#include "identity.h"
#include "ban.h"

void usage(char* name) {
    fprintf(stdout, "Example Usage: %s [-i siip] [-p sipt] %s \n", name, _VERBOSE_OPT_SHOW);
    fprintf(stdout, "Arguments:\n"
            "\t-i\t\t[server ip]\n"
            "\t-p\t\t[server port]\n"
            "%s", _VERBOSE_OPT_INFO);
}

int main(int argc, char *argv[]) {
    char server_ip[STRING_SIZE] = "tejo.tecnico.ulisboa.pt";
    char server_port[STRING_SIZE] = "59000";

    srand(time(NULL));
    // Treat options
    int_fast8_t oc  = 0;
    while ((oc = getopt(argc, argv, "i:p:v")) != -1) { //Command-line args parsing, 'i' and 'p' args required for both
        switch (oc) {
            case 'i':
                strncpy(server_ip, optarg, STRING_SIZE); //optarg has the string corresponding to oc value
                break;
            case 'p':
                strncpy(server_port, optarg, STRING_SIZE); //optarg has the string corresponding to oc value
                break;
            case ':':
                /* missing option argument */
                fprintf(stderr, "%s: option '-%c' requires an argument\n",
                        argv[0], optopt);
                break;
            case 'v':
                _VERBOSE_OPT_CHECK;
            case '?': //Left blank on purpose, help option
            default:
                usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    fprintf(stdout, KBLU "Identity Server:" KNRM " %s:%s\n", server_ip, server_port);
    struct addrinfo *id_server = get_server_address(server_ip, server_port);
    if (!id_server) {
        fprintf(stderr, KRED "Unable to parse id server address from:\n %s:%s", server_ip, server_port);
        return EXIT_FAILURE;
    }

    int_fast32_t outgoing_fd = -1, binded_fd = -1, timer_fd = -1;
    uint_fast8_t exit_code = EXIT_SUCCESS, err = EXIT_SUCCESS, max_fd = -1;
    uint_fast32_t msg_num = 0;

    list msgservers_lst = NULL;
    server sel_server;

    struct itimerspec new_timer = {
        {SERVER_TEST_TIME_SEC,SERVER_TEST_TIME_nSEC},   //Interval of time
        {SERVER_TEST_TIME_SEC,SERVER_TEST_TIME_nSEC}    //Stop time
        };


    if (1 == init_program(id_server, &outgoing_fd,
                &binded_fd, &msgservers_lst, &sel_server,
                &new_timer, &timer_fd)){
        exit_code = EXIT_FAILURE;
        goto PROGRAM_EXIT;
    }

    fd_set rfds;
    bool server_not_answering = false;
    char op[STRING_SIZE], input_buffer[STRING_SIZE];
    list banservers_lst = create_list();

    if (sel_server != NULL) {
        fprintf(stdout, KGRN "Prompt > " KNRM);
        fflush(stdout);
    }

    // Interactive loop
    while (true) {
        FD_ZERO(&rfds);
        FD_SET(binded_fd, &rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(timer_fd, &rfds);

        max_fd = binded_fd > max_fd ? binded_fd : max_fd;
        max_fd = timer_fd > max_fd ? timer_fd : max_fd;

        if (NULL == sel_server || err || server_not_answering) {
            fprintf(stderr, KYEL "Searching\n" KNRM);
            sleep(3);

            if (server_not_answering) { //Sends to bans list
            	if(sel_server != NULL){
                    ban_server(banservers_lst, sel_server);
                }
            }

            if (server_not_answering) {
                rem_awol_server(msgservers_lst, sel_server);
            }
            
            sel_server = select_server(msgservers_lst);
            while (sel_server != NULL && is_banned(banservers_lst, sel_server)){
                rem_awol_server(msgservers_lst, sel_server);
                sel_server = select_server(msgservers_lst);
            }

            if (sel_server != NULL) {
                fprintf(stderr, KGRN "Connected to new server\n" KNRM);
                fprintf(stdout, KGRN "Prompt > " KNRM);
                fflush(stdout);
                server_not_answering = false;
            } else {
                fprintf(stderr, KYEL "No servers available..." KNRM);
                fflush(stdout);
                free_list(msgservers_lst, free_server); //Get new servers if the list is all run
                msgservers_lst = fetch_servers(outgoing_fd, id_server);
                sel_server = NULL;
                server_not_answering = false;
            }
            continue;
        }

        int activity = select(max_fd + 1 , &rfds, NULL, NULL, NULL); //Select, threading function
        if (0 > activity) {
            printf("error on select\n%d\n", errno);
            exit_code = EXIT_FAILURE;
            goto PROGRAM_EXIT;
        }

        if (FD_ISSET(timer_fd, &rfds)) { //if the timer is triggered
            uint_fast8_t server_test_status = exec_server_test();

            if (1 == server_test_status) {
                printf(KYEL "Server not answering\n" KNRM);
                fflush(stdout);
                server_not_answering = true;
            }

            timerfd_settime (timer_fd, 0, &new_timer, NULL);
            continue;
        }

        if (FD_ISSET(binded_fd, &rfds)) { //UDP receive
            if (2 == handle_incoming_messages(binded_fd, msg_num)) {
                fprintf(stdout, KGRN "Prompt > " KNRM);
                fflush(stdout);
            }
        }

        if (FD_ISSET(STDIN_FILENO, &rfds)) { //Stdio input
            scanf("%s%*[ ]%140[^\t\n]" , op, input_buffer); // Grab word, then throw away space and finally grab until \n

            //User options input: show_servers, exit, publish message, show_latest_messages n;
            if (0 == strcasecmp("show_servers", op) || 0 == strcmp("0", op)) {
                print_list(msgservers_lst, print_server);
            } else if (0 == strcasecmp("publish", op) || 0 == strcmp("1", op)) {
                if (0 == strlen(input_buffer)) {
                    continue;
                }
                err = publish(binded_fd, sel_server, input_buffer);
                if (err) {
                    fprintf(stderr, KRED "Publish error\n" KNRM);
                }
                err = ask_for_messages(binded_fd, sel_server, 0);
                if (err) {
                    fprintf(stderr, KRED "Ask for messages error\n" KNRM);
                }
                ask_server_test();

            } else if (0 == strcasecmp("show_latest_messages", op) || 0 == strcmp("2", op)) {
                int msg_num_test = atoi(input_buffer);
                if( 0 < msg_num_test) {
                    msg_num = msg_num_test;
                    err = ask_for_messages(binded_fd, sel_server, msg_num);
                    ask_server_test();
                }
                else {
                    printf(KRED "%s is invalid value, must be positive\n" KNRM, input_buffer);
                }
            }else if (0 == strcasecmp("show_selected_server", op) || 0 == strcmp("s", op)) {
                print_server(sel_server);
            }else if (0 == strcasecmp("exit", op) || 0 == strcmp("3", op)) {
                exit_code = EXIT_SUCCESS;
                goto PROGRAM_EXIT;
            } else {
                fprintf(stderr, KRED "%s is an unknown operation\n" KNRM, op);
            }

            bzero(op, STRING_SIZE);
            bzero(input_buffer, STRING_SIZE);

            fprintf(stdout, KGRN "Prompt > " KNRM);
            fflush(stdout);
        }
    }

PROGRAM_EXIT:
    freeaddrinfo(id_server);
    free_incoming_messages();
    free_list(msgservers_lst, free_server);
    if(banservers_lst) free_list(banservers_lst, free_server);
    close(outgoing_fd);
    close(binded_fd);
    return exit_code;
}
