//
// Created by root on 3/20/16.
//
#include <string.h>
#include <assert.h>
#include "handle_read.h"


typedef enum {
    PARSE_SUCCESS    = 1 << 1, /* Parse the Reading Success, set the event to Write Event */
    PARSE_BAD_SYNTAX = 1 << 2, /* Parse the Reading Fail, for the Wrong Syntax */
    PARSE_BAD_REQUT  = 1 << 3, /* Parse the Reading Success, but Not Implement OR No Such Resources*/
}PARSE_STATUS;

/* Parse the Reading thing, and Make deal with them
 * */
static PARSE_STATUS parse_reading(conn_client * client);
static int read_n(conn_client * client);

HANDLE_STATUS handle_read(conn_client * client) {
    int err_code = 0;

    /* Reading From Socket */
    err_code = read_n(client);
    if (err_code != READ_SUCCESS) { /* If read Fail then End this connect */
        return HANDLE_READ_FAILURE;
    }
#if defined(WSX_DEBUG)
    fprintf(stderr, "\nRead From Client(%d): %s\n read %d Bytes\n", client->file_dsp, client->r_buf->str, client->r_buf_offset);
    fprintf(stderr, "\nStart Parsing Reading\n");
#endif
    /* Parsing the Reading Data */
    err_code = parse_reading(client);
    if(err_code != PARSE_SUCCESS) { /* If Parse Fail then End this connect */
        return HANDLE_READ_FAILURE;
    }
    return HANDLE_READ_SUCCESS;
}

/*
 * Read data from peer(TCP Read buffer)
 * read_buf is aim to be a local buffer
 * client->r_buf is the real Storage of the data
 * */
static int read_n(conn_client * client) {
    /* -
    if (client->read_offset >= CONN_BUF_SIZE-1)
        return READ_OVERFLOW;
    */
    int    fd        = client->file_dsp;
    char * buf       = client->read_buf;
    int    buf_index = client->read_offset;
    int read_number = 0;
    int less_capacity = 0;
    while (1) {
        less_capacity = CONN_BUF_SIZE - buf_index;
        if (less_capacity <= 1) {/* Overflow Protection */
            buf[buf_index] = '\0'; /* Flush the buf to the r_buf String */
            client->r_buf->use->append(client->r_buf, APPEND(buf));
            client->r_buf_offset += client->read_offset;
            client->read_offset = buf_index = 0;
            buf = client->read_buf;
            less_capacity = CONN_BUF_SIZE - buf_index;
        }
        read_number = read(fd, buf+buf_index, less_capacity);
        if (0 == read_number) { /* We must close connection */
            return READ_FAIL;
        }
        else if (-1 == read_number) { /* Nothing to read */
            if (EAGAIN == errno || EWOULDBLOCK == errno) {
                buf[buf_index] = '\0';
                client->r_buf->use->append(client->r_buf, APPEND(buf));
                client->r_buf_offset += client->read_offset;
                return READ_SUCCESS;
            }
            return READ_FAIL;
        }
        else { /* Continue to Read */
            buf_index += read_number;
            client->read_offset = buf_index;
        }
    }
}

/******************************************************************************************/
/* Deal with the read buf data which has been read from socket */
#define METHOD_SIZE 128
#define PATH_SIZE METHOD_SIZE*2
#define VERSION_SIZE METHOD_SIZE
typedef struct requ_line{
    char path[PATH_SIZE];
    char method[METHOD_SIZE];
    char version[VERSION_SIZE];
}requ_line;
static int get_line(conn_client * restrict client, char * restrict line_buf, int max_line);
static DEAL_LINE_STATUS deal_requ(conn_client * client,  const requ_line * status);
static DEAL_LINE_STATUS deal_head(conn_client * client);

/*
 * Now we believe that the GET has no request-content(Request Line && Request Head)
 * The POST Will be considered later
 * parse_reading will parse the request line and Request Head,
 * and Prepare the Page which will set into the Write buffer
 * */
PARSE_STATUS parse_reading(conn_client * client) {
    int err_code = 0;
    requ_line line_status = {0};
    client->read_offset  = 0; /* Set the local buffer offset to 0, the end of buf is '\0' */
    client->r_buf_offset = 0; /* Set the real Storage offset to 0, the end of buf is '\0' */

    /* Get Request line */
    err_code = deal_requ(client, &line_status); /* First Line in Request */
    if (DEAL_LINE_REQU_FAIL == err_code)
        return PARSE_BAD_REQUT;

#if defined(WSX_DEBUG)
    fprintf(stderr, "Starting Deal_head\n");
#endif
    /* Get Request Head Attribute until /r/n */
    err_code = deal_head(client);               /* The second line to the Empty line */
    if (DEAL_HEAD_FAIL == err_code)
        return PARSE_BAD_SYNTAX;
    
    /* Response Page maker */
    err_code = make_response_page(client);  
    if (MAKE_PAGE_FAIL == err_code)
        return PARSE_BAD_REQUT;
    return PARSE_SUCCESS;
}
/*
 * deal_requ, get the request line
 * */
static DEAL_LINE_STATUS deal_requ(conn_client * client, const requ_line * status) {
#define READ_HEAD_LINE 256
    char requ_line[READ_HEAD_LINE] = {'\0'};
    int err_code = get_line(client, requ_line, READ_HEAD_LINE);
#if defined(WSX_DEBUG)
    assert(err_code > 0);
#endif
    if (err_code < 0)
        return DEAL_LINE_REQU_FAIL;
#if defined(WSX_DEBUG)
    fprintf(stderr, "Requset line is : %s \n", requ_line);
#endif
    /* For example GET / HTTP/1.0 */
    err_code = sscanf(requ_line, "%s %s %s", status->method, status->path, status->version);
    if (err_code != 3)
        return DEAL_LINE_REQU_FAIL;
    (client->conn_res).requ_method->use->append((client->conn_res).requ_method, APPEND(status->method));
    (client->conn_res).requ_http_ver->use->append((client->conn_res).requ_http_ver, APPEND(status->version));
    (client->conn_res).requ_res_path->use->append((client->conn_res).requ_res_path, APPEND(status->path));
#if defined(WSX_DEBUG)
    fprintf(stderr, "The Request method : %s, path : %s, version : %s\n",
                                    status->method, status->path, status->version);
    fprintf(stderr, "[String] The Request method : \n");
    client->conn_res.requ_res_path->use->print(client->conn_res.requ_res_path);
    client->conn_res.requ_http_ver->use->print(client->conn_res.requ_http_ver);
    client->conn_res.requ_method->use->print(client->conn_res.requ_method);
#endif
    return DEAL_LINE_REQU_SUCCESS;
#undef READ_HEAD_LINE
}
/*
 * get the request head
 * */
static DEAL_LINE_STATUS deal_head(conn_client * client) {
    /* TODO
     * Complete the Function of head attribute
     * */
#define READ_ATTRIBUTE_LINE 256
    int nbytes = 0;
    char head_line[READ_ATTRIBUTE_LINE] = {'\0'};
    while((nbytes = get_line(client, head_line, READ_ATTRIBUTE_LINE)) > 0) {
        if(0 == strncmp(head_line, "\r\n", 2)) {
#if defined(WSX_DEBUG)
            fprintf(stderr, "Read the empty Line\n");
#endif
            break;
        }
#if defined(WSX_DEBUG)
        fprintf(stderr, "The %d Line we parse(%d Bytes) : ", index++, nbytes);
        fprintf(stderr, "%s", head_line);
#endif
    }
    if (nbytes < 0) {
        fprintf(stderr, "Error Reading in deal_head\n");
        return DEAL_HEAD_FAIL;
    }
#if defined(WSX_DEBUG)
    fprintf(stderr, "Deal head Success\n");
#endif
    return DEAL_HEAD_SUCCESS;
#undef READ_ATTRIBUTE_LINE
}

/* Get One Line(\n\t) From The Reading buffer
 * */
static int get_line(conn_client * restrict client, char * restrict line_buf, int max_line) {
    int nbytes = 0;
    char *r_buf_find = (client->r_buf->use->has(client->r_buf,"\n"));
    if (NULL == r_buf_find){ 
        fprintf(stderr, "get_line has read a 0\n");
        return READ_BUF_FAIL;
    } else{
        nbytes = r_buf_find - (client->r_buf->str + client->r_buf_offset) + 1;
        if (max_line-1 < nbytes)
            return READ_BUF_OVERFLOW;
        memcpy(line_buf, client->r_buf->str+client->r_buf_offset, nbytes);
        client->r_buf_offset = r_buf_find - client->r_buf->str + 1;
        *r_buf_find = '\r'; /* Let the \n to be \r */
    }
    line_buf[nbytes] = '\0';
    return nbytes;
}
