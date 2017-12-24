//
// Created by tianqi on 17-12-23.
//

#include "websocket_session_init.h"

#include <openssl/sha.h>

int deal_ws_request(int new_fd, struct sockaddr_in *client_addr){
    char recv_buf[HEAD_MAX_SIZE + 1] = "";
    char send_buf[HEAD_MAX_SIZE + 1] = "";

    int read_byte = 0;
    int file_byte = 0;


    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(new_fd, &read_set);

    struct timeval timeout;
    timeout.tv_sec = TIME_OUT_SEC;
    timeout.tv_usec = TIME_OUT_USEC;

    struct ws_request *req
            = (struct ws_request *)malloc(sizeof(struct ws_request));
    strcpy(req->address, inet_ntoa(client_addr->sin_addr));
    req->port = client_addr->sin_port;
    req->client_socket = new_fd;
    req->method_right_state = false;

    int resel = 0;
    int maxfd = new_fd+1;

    while(true){
        resel = select(maxfd, &read_set, NULL, NULL, &timeout);
        switch (resel) {
            case -1:
                perror("Cannot select read_fd!");
                return -1;
            case 0:
                printf("Listen time out!\n");
                return 1;
            default:
                if(FD_ISSET(new_fd, &read_set)){
                    read_byte = (int)recv(new_fd, recv_buf, 16384, 0);
                    if(read_byte == 0){
                        return 0;     //connect interrupt!
                    } else if(read_byte < 0) {
                        perror("Read request failure!");
                        return -1;
                    } else {
                        if(is_ws_request(recv_buf,req) == -1){
                            printf("WebSocket request is illegal!\n");
                            return -1;
                        }
                        if(do_ws_response(req) == -1){
                            printf("Send WebSocket response failure!\n");
                            return -1;
                        }
                        return 1;
                    }
                } else {
                    perror("FD set happen crash!");
                    continue;
                }
        }
    }
}

int _readline(char* allbuf,int level,char* linebuf){
    int len = strlen(allbuf);
    for (;level<len;++level)
    {
        if(allbuf[level]=='\r' && allbuf[level+1]=='\n')
            return level+2;
        else
            *(linebuf++) = allbuf[level];
    }
    return -1;
}


int is_ws_request(char *request, struct ws_request* req){
    char line_buf[LINE_MAX_SIZE];
    int read_index = 0;

    if((read_index = _readline(request, read_index, line_buf)) == -1){
        printf("Request cannot been read!\n");
        return -1;
    }
    if(is_http_head(line_buf, req) == -1){
        printf("Request is not a HTTP head!\n");
        return -1;
    }
    if(analysis_ws_info(request,read_index, req) == -1){
        printf("Cannot analysis the head!\n");
        return -1;
    }
    return 1;
}

int is_http_head(char *f_line, struct ws_request *req){
    char me[8] = "";
    char uri[128] = "";

    sscanf(f_line,"%s %s %s",
           me, uri, req->protocol);

    strcpy(req->uri, uri);

    if(is_right_ws_method(&(req->method_right_state), me) == -1){
        req->state = 405;
        printf("WebSocket request file has wrong method!");
        return -1;
    }

    if(is_right_ws_protocol(req->protocol) == -1){
        req->state = 505;
        printf("WebSocket request file has wrong protocol!");
        return -1;
    }

    return 1;
}

int is_right_ws_method(bool *meth, char *method){
    if(strcmp(method, "GET") == 0){
        *meth = true;
        return 1;
    } else {
        *meth = false;
        return -1;
    }
}

int is_right_ws_protocol(char *protocol){
    if(strcmp(protocol, "HTTP/1.1") != 0){
        return -1;
    } else {
        return 1;
    }
}

int analysis_ws_info(char *request, int index, struct ws_request *req){
    char line_buf[LINE_MAX_SIZE + 1] = "";
    char left[64], right[64];
    int ver = 0;
    while((index = _readline(request, index, line_buf)) != -1){
        if(strstr(line_buf, "Host") != NULL){
            sscanf(line_buf, "%s%s", left, right);
            strcpy(req->host, right);
        }
        if(strstr(line_buf, "Connection") != NULL){
            sscanf(line_buf, "%s%s", left, right);
            if(strcmp(right, "Upgrade") == 0){
                req->connection_is_up = true;
            } else {
                req->connection_is_up = false;
                printf("Request is not to Upgrade!\n");
                return -1;
            }
        }
        if(strstr(line_buf, "Upgrade") != NULL){
            sscanf(line_buf, "%s%s", left, right);
            if(strcmp(left, "Upgrade") == 0){
                if(strcmp(right, "websocket") == 0){
                    req->upgrade_is_ws = true;
                } else {
                    req->upgrade_is_ws = false;
                    printf("Request is not to Upgrade to websocket!\n");
                    return -1;
                }
            }
        }
        if(strstr(line_buf, "Sec-WebSocket-Version") != NULL){
            sscanf(line_buf, "%s%s", left, right);
            sprintf(right, "%d", ver);
            req->sec_websocket_version = ver;
        }
        if(strstr(line_buf, "Accept-Language") != NULL){
            sscanf(line_buf, "%s%s", left, right);
            strcpy(req->accept_language, right);
        }
        if(strstr(line_buf, "Origin") != NULL){
            sscanf(line_buf, "%s%s", left, right);
            strcpy(req->origin, right);
        }
        if(strstr(line_buf, "Sec-WebSocket-Key") != NULL){
            sscanf(line_buf, "%s%s", left, right);
            strcpy(req->sec_websocket_key, right);
        }
        if(strstr(line_buf, "Sec-WebSocket-Extensions") != NULL){
            sscanf(line_buf, "%s%s", left, right);
            strcpy(req->sec_websocket_extensions, right);
        }
        memset(line_buf, 0, LINE_MAX_SIZE +1);
    }
    return 1;
}

int do_ws_response(struct ws_request *req){
    char head[5][256];
    char res[HEAD_MAX_SIZE +1] = "";
    if(get_ws_first_line(head[0]) == -1){
        printf("First line error\n");
        return -1;
    }
    if(get_upgreade_line(head[1]) == -1){
        printf("Upgrade line error\n");
        return -1;
    }
    if(get_connection_line(head[2]) == -1){
        printf("Connection line error\n");
        return -1;
    }
    if(get_sec_WA_line(head[3], req->sec_websocket_key) == -1){
        printf("Sec-WebSocket-Accept line error\n");
        return -1;
    }
    if(get_sec_WP_line(head[4], req->uri) == -1){
        printf("Sec-WebSocket-Protocol line error\n");
        return -1;
    }

    for(int i = 0; i < 5; i++){
        strcat(res, head[i]);
    }

    if(send(req->client_socket, res, strlen(res), 0) <= -1){
        printf("%s", "Client close the connection!\n");
        return 1;
    }
    printf("Send response success!\n");
    return 1;
}

int get_ws_first_line(char *f_line){
    char line[64] = "HTTP/1.1 101 Switching Protocols\r\n";
    strcpy(f_line, line);
    return 1;
}

int get_upgreade_line(char *f_line){
    char line[64] = "Upgrade: websocket\r\n";
    strcpy(f_line, line);
    return 1;
}

int get_connection_line(char *f_line){
    char line[64] = "Connection: Upgrade\r\n";
    strcpy(f_line, line);
    return 1;
}

int get_sec_WA_line(char *f_line, char *key){
    char line[256] = "Sec-WebSocket-Accept: ";
    char accpet[256] = "";
    char combine[256];
    char sha_data[SHA_DIGEST_LENGTH + 1] = "";
    memcpy(combine, key, strlen(key));
    memcpy(combine + strlen(key), GUID, strlen(GUID));
    SHA1((unsigned char *)&combine, strlen(combine), (unsigned char *)&sha_data);
    strcpy(accpet, (const char *)base64_encode((unsigned char *)sha_data));
    memcpy(line + strlen(line), accpet, sizeof(accpet));
    memcpy(f_line, line, sizeof(line));

    return 1;
}

int get_sec_WP_line(char *f_line, char *name){
    char line[64] = "Sec-WebSocket-Protocol: ";
    strcat(line, name+1);
    strcat(line, "\r\n");
    strcpy(f_line, line);
    return 1;
}