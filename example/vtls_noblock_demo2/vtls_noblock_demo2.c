/*****************************************************************/ /**
* @file vtls_noblock_demo2.c
* @brief
* @author larson.li@quectel.com
* @date 2023-08-17
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
*
* @par EDIT HISTORY FOR MODULE
* <table>
* <tr><th>Date <th>Version <th>Author <th>Description
* <tr><td>2023-08-17 <td>1.0 <td>Larson.Li <td> Init
* </table>
**********************************************************************/
#include "qosa_def.h"
#include "qosa_sys.h"
#include "qcm_vtls.h"
#include "qosa_log.h"
#include "qosa_rtc.h"
#include "qosa_sockets.h"
#include "qosa_datacall.h"
#include "unirtos_app_init_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QOS_LOG_TAG                        LOG_TAG

#define VTLS_NOBLOCK_DEMO2_TASK_STACK_SIZE 6144

#define VTLS_NOBLOCK_DEMO_SIMID            0
#define VTLS_NOBLOCK_DEMO_PDPID            1
#define VTLS_NOBLOCK_DEMO_ACTIVE_TIMEOUT   30  // PDP activation timeout 30s

/** The content of the data to be sent */
#define GET_REQUEST                        "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nAccept: */*\r\n\r\n"

#define VTLS_NOBLOCK_SERVER_PORT           443
#define VTLS_NOBLOCK_SERVER_NAME           "www.baidu.com"
#define SEND_BUFF_MAX_LEN                  1024

/*===========================================================================
 *
 ===========================================================================*/

/**
 * @brief Check and activate PDP
 *
 * This function is used to check the data connection status of the specified SIM card and PDP context,
 * and performs synchronous activation if not activated.
 * Uses predefined SIMID and PDPID parameters to establish the data connection.
 *
 * @return int Execution result
 * @retval 0  Successfully activated data connection or connection is already active
 * @retval -1 Failed to activate data connection
 */
static int unir_vtls_noblock_datacall_active(void)
{
    /* Define data connection related variables */
    qosa_datacall_conn_t    conn = {0};
    qosa_datacall_ip_info_t info = {0};
    qosa_datacall_errno_e   ret = 0;

    /* Create new data connection object */
    conn = qosa_datacall_conn_new(VTLS_NOBLOCK_DEMO_SIMID, VTLS_NOBLOCK_DEMO_PDPID, QOSA_DATACALL_CONN_TCPIP);

    /* Check data connection information to determine if PDP is already activated */
    if (QOSA_DATACALL_ERR_NO_ACTIVE == qosa_datacall_get_ip_info(conn, &info))
    {
        QLOGD("sim_id=%d,pdp_id=%d", VTLS_NOBLOCK_DEMO_SIMID, VTLS_NOBLOCK_DEMO_PDPID);

        /* If PDP is not activated, start synchronous activation process */
        ret = qosa_datacall_start(conn, VTLS_NOBLOCK_DEMO_ACTIVE_TIMEOUT);
        if (QOSA_DATACALL_OK != ret)
        {
            QLOGE("datacall ret=%x", ret);
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Get IP address corresponding to hostname through DNS resolution
 *
 * This function uses getaddrinfowithcid for DNS resolution, converts the hostname to IPv4 address,
 * and stores the result in the provided ip buffer.
 *
 * @param hostname Pointer to the hostname string to resolve
 * @param ip Pointer to character array for storing the resolved IP address
 *
 * @return 0 on success, -1 on failure
 */
static int unir_vtls_noblock_dns(char *hostname, char *ip, qosa_uint32_t ip_len)
{
    struct addrinfo     hints = {0};
    struct addrinfo    *result = QOSA_NULL;
    struct sockaddr_in *ipv4 = QOSA_NULL;
    int                 status = 0;

    QLOGD("hostname=%s", hostname);

    // DNS resolution configuration
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    status = getaddrinfowithcid(hostname, QOSA_NULL, &hints, &result, VTLS_NOBLOCK_DEMO_PDPID);
    if (status != 0)
    {
        QLOGE("dns err=%d", status);
        return -1;
    }

    // Extract and convert IP address
    ipv4 = (struct sockaddr_in *)result->ai_addr;
    inet_ntop(AF_INET, &(ipv4->sin_addr), ip, ip_len);
    QLOGD("ip=%s", ip);
    freeaddrinfo(result);
    return 0;
}

/**
 * @brief Configure custom socket IO read function
 *
 * @param[in] void * ctx
 *          - Pointer address of qcm_ssl_connect_data_t type allocated for current socket
 *
 * @param[out] unsigned char * buf
 *          - Starting address of data content to be written
 *
 * @param[in] qosa_size_t len
 *          - Corresponds to the current data length to be written
 *
 * @return int
 *       - Returns actual read length on success
 *       - Returns QCM_VTLS_IO_ERR_SSL_WANT_READ if socket enters wouldblock state
 *       - Returns QCM_VTLS_IO_ERR_SSL_CONN_RESET if socket connection is disconnected
 */
static int block_io_read(void *ctx, unsigned char *buf, qosa_size_t len)
{
    int                     ret = 0;
    qcm_ssl_connect_data_t *ssl_ctx = (qcm_ssl_connect_data_t *)ctx;

    ret = read(ssl_ctx->ssl_config.socket_fd, buf, len);
    QLOGD("ret=%d", ret);

    if (ret < 0)
    {
#ifdef CONFIG_QOSA_LINUX_PLATFORM_FUNC
        if (errno == EWOULDBLOCK)
#else
        if (lwip_get_error(ssl_ctx->ssl_config.socket_fd) == EWOULDBLOCK)
#endif
        {
            ret = QCM_VTLS_IO_ERR_SSL_WANT_READ;
        }
        else
        {
            ret = QCM_VTLS_IO_ERR_SSL_CONN_RESET;
        }
    }
    else if (ret == 0)
    {
        if (len == 0)
        {
            ret = QCM_VTLS_IO_ERR_SSL_WANT_READ;
        }
        else
        {
            ret = QCM_VTLS_IO_ERR_SSL_CONN_RESET;
        }
    }

    return ret;
}

/**
 * @brief Configure custom socket IO write function
 *
 * @param[in] void * ctx
 *          - Pointer address of qcm_ssl_connect_data_t type allocated for current socket
 *
 * @param[in] unsigned char * buf
 *          - Starting address of data content to be written
 *
 * @param[in] qosa_size_t len
 *          - Corresponds to the current data length to be written
 *
 * @return int
 *       - Returns actual write length on success
 *       - Returns QCM_VTLS_IO_ERR_SSL_WANT_WRITE if socket enters wouldblock state
 *       - Returns QCM_VTLS_IO_ERR_SSL_CONN_RESET if socket connection is disconnected
 */
static int block_io_write(void *ctx, const unsigned char *buf, qosa_size_t len)
{
    int                     ret = 0;
    qcm_ssl_connect_data_t *ssl_ctx = (qcm_ssl_connect_data_t *)ctx;

    ret = write(ssl_ctx->ssl_config.socket_fd, buf, len);
    QLOGD("ret=%d", ret);
    if (ret < 0)
    {
#ifdef CONFIG_QOSA_LINUX_PLATFORM_FUNC
        if (errno == EWOULDBLOCK)
#else
        if (lwip_get_error(ssl_ctx->ssl_config.socket_fd) == EWOULDBLOCK)
#endif
        {
            ret = QCM_VTLS_IO_ERR_SSL_WANT_WRITE;
        }
        else
        {
            ret = QCM_VTLS_IO_ERR_SSL_CONN_RESET;
        }
    }
    else if (ret == 0)
    {
        if (len == 0)
        {
            ret = QCM_VTLS_IO_ERR_SSL_WANT_WRITE;
        }
        else
        {
            ret = QCM_VTLS_IO_ERR_SSL_CONN_RESET;
        }
    }

    return ret;
}

/**
 * @brief Socket event notification function, not limited to select, epoll or poll functions
 *        Main purpose is to wait for read or write events to arrive while also having timeout timer function
 *
 * @param[in] void * ctx
 *          - Pointer address of qcm_ssl_connect_data_t type allocated for current socket
 *
 * @param[in] qosa_bool_t read_flag
 *          - Whether to monitor read events
 *
 * @param[in] qosa_bool_t write_flag
 *          - Whether to monitor write events
 *
 * @param[in] qosa_uint32_t wait_time
 *          - Maximum time for function to block and wait
 *
 * @return int
 *       - Returns actual number of events on success
 *       - Returns 0 to indicate timeout
 *       - Returns negative number for other reasons
 */
static int block_io_select(void *ctx, qosa_bool_t read_flag, qosa_bool_t write_flag, qosa_uint32_t wait_time)
{
    fd_set                  readset;
    fd_set                  writeset;
    int                     ret = 0;
    struct timeval          tm;
    qcm_ssl_connect_data_t *ssl_ctx = (qcm_ssl_connect_data_t *)ctx;
    FD_ZERO(&readset);
    FD_ZERO(&writeset);
    if (read_flag == QOSA_TRUE)
    {
        FD_SET(ssl_ctx->ssl_config.socket_fd, &readset);
    }
    if (write_flag == QOSA_TRUE)
    {
        FD_SET(ssl_ctx->ssl_config.socket_fd, &writeset);
    }

    tm.tv_sec = wait_time;
    tm.tv_usec = 0;

    ret = select(ssl_ctx->ssl_config.socket_fd + 1, &readset, &writeset, QOSA_NULL, &tm);
    QLOGD("ret=%d", ret);
    return ret;
}

/**
 * @brief Create socket and connect, using blocking method to connect to target host
 *
 * @param[in] const char * ip
 *          - Target host IP address, must be in dotted decimal format IP address (DNS not resolved)
 *
 * @param[in] qosa_uint16_t port
 *          - Target host port
 *
 * @return int
 *       - Returns connected socket descriptor on success
 *       - Returns -1 on failure
 */
static int unir_vtls_socket_create(const char *local_ip, const char *remote_ip, qosa_uint16_t port)
{
    int                client_socket = -1;
    struct sockaddr_in server_addr = {0};
    struct sockaddr_in local_addr = {0};
    // Create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1)
    {
        QLOGE("socket create error");
        return -1;
    }

    // Set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(remote_ip);
    server_addr.sin_port = htons(port);

    // Bind to local IP address

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr(local_ip);
    if (bind(client_socket, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0)
    {
        close(client_socket);
        QLOGE("socket bind error");
        return -1;
    }
    // Connect to server
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        close(client_socket);
        QLOGE("socket connect error");
        return -1;
    }

    return client_socket;
}

/**
 * @brief SSL connection using non-blocking socket IO for SSL connection testing
 */
static void unir_ssl_noblock_demo2_thread(void *argv)
{
    fd_set                  readset;                           // Read socket character set used by select
    struct timeval          tm;                                // Maximum single wait time used by select
    int                     result_code = 0;                   // Return SSL function execution result
    qcm_ssl_connect_data_t *ssl_ctx = QOSA_NULL;               // SSL handle connection information
    qcm_ssl_config_t        ssl_config = {0};                  // SSL connection config configuration
    int                     client_socket;                     // Socket descriptor handle
    char                    buffer[SEND_BUFF_MAX_LEN + 1];     // Used for read/write data
    int                     flags = 0;                         // Used to set socket to non-blocking IO
    qosa_bool_t             done = QOSA_FALSE;                 // SSL connect function execution completion flag (does not indicate connection success/failure)
    char                    remote_ip[INET_ADDRSTRLEN] = {0};  // Used to store remote server IP address
    char                    local_ip[INET_ADDRSTRLEN] = {0};   // Used to store local IP address
    int                     ret = 0;

    QOSA_UNUSED(argv);

    qosa_task_sleep_sec(10);
    // Network activation
    ret = unir_vtls_noblock_datacall_active();
    if (ret != 0)
    {
        QLOGE("pdp err");
        return;
    }
    // DNS resolution
    ret = unir_vtls_noblock_dns(VTLS_NOBLOCK_SERVER_NAME, remote_ip, INET_ADDRSTRLEN);
    if (ret != 0)
    {
        QLOGE("dns err");
        return;
    }

    qosa_datacall_conn_t    conn = QOSA_NULL;
    qosa_datacall_ip_info_t info = {0};

    conn = qosa_datacall_conn_new(VTLS_NOBLOCK_DEMO_SIMID, VTLS_NOBLOCK_DEMO_PDPID, QOSA_DATACALL_CONN_TCPIP);
    if (qosa_datacall_get_ip_info(conn, &info) == QOSA_DATACALL_OK)
    {
        inet_ntop(AF_INET, &(info.ipv4_ip.apptcpip_ipv4_addr), local_ip, INET_ADDRSTRLEN);
        QLOGD("local_ip=%s", local_ip);
    }
    else
    {
        QLOGE("get local ip error");
        return;
    }

    // Traverse DNS returned address list, attempt to establish TCP connection

    // Use qcm_socket_create to create blocking socket

    // Create socket and connect, bind to local IP obtained from PDP

    client_socket = unir_vtls_socket_create(local_ip, remote_ip, VTLS_NOBLOCK_SERVER_PORT);
    if (client_socket == -1)
    {
        QLOGE("%s:%p tcp connect error!!\n", remote_ip, VTLS_NOBLOCK_SERVER_PORT);
        return;
    }
    QLOGV("socket connect success!! \n");

    // Set to non-blocking
    flags = fcntl(client_socket, F_GETFL, 0);
    fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);

    // Configure SSL connection related configuration information
    qosa_memset(&ssl_config, 0x00, sizeof(qcm_ssl_config_t));
    ssl_config.ssl_version = QCM_SSL_VERSION_ALL;
    ssl_config.auth_mode = QCM_SSL_VERIFY_NULL;
    ssl_config.transport = QCM_SSL_TLS_PROTOCOL;
    ssl_config.socket_fd = client_socket;
    ssl_config.ssl_negotiate_timeout = 30;  // SSL handshake timeout
    ssl_config.io_read = block_io_read;
    ssl_config.io_select = block_io_select;
    ssl_config.io_write = block_io_write;
    ssl_config.sni_enable = QOSA_TRUE;     // Enable SSL SNI option
    ssl_config.ssl_log_debug = QOSA_TRUE;  // Enable SSL log output option

    // Apply for SSL connection handle
    ssl_ctx = qcm_ssl_new(&ssl_config);
    if (ssl_ctx == QOSA_NULL)
    {
        close(client_socket);
        return;
    }

    // Copy host domain name, if SNI is not enabled, this step is not needed, hostname doesn't need to be freed separately, it will be released in qcm_ssl_free
    ssl_ctx->hostname = qosa_malloc(qosa_strlen(VTLS_NOBLOCK_SERVER_NAME) + 1);
    if (ssl_ctx->hostname == QOSA_NULL)
    {
        close(client_socket);
        qcm_ssl_free(ssl_ctx);
        return;
    }
    qosa_strcpy(ssl_ctx->hostname, VTLS_NOBLOCK_SERVER_NAME);

    // Set SSL current connection start time
    ssl_ctx->cur_connect_time = qosa_get_system_time_seconds();

    // Execute SSL connection, using socket non-blocking IO and SSL non-blocking interface, function returns 0 indicates not completed and needs to continue execution waiting
    ret = qcm_ssl_connect_nonblocking(ssl_ctx, &done);
    if (ret != 0)
    {
        QLOGE("ssl_handshark error!!!");
        goto exit;
    }

    while (1)
    {
        FD_SET(client_socket, &readset);
        tm.tv_sec = ssl_config.ssl_negotiate_timeout;
        tm.tv_usec = 0;

        // select blocks waiting for server to send data
        ret = select(client_socket + 1, &readset, QOSA_NULL, QOSA_NULL, &tm);
        QLOGD("ret=%d", ret);

        // Received socket read event, read data
        if (FD_ISSET(client_socket, &readset))
        {
            // If current SSL connection is not completed, execute qcm_ssl_connect_nonblocking function again
            if (ssl_ctx->state != QCM_SSL_CONNECTION_COMPLETE)
            {
                ret = qcm_ssl_connect_nonblocking(ssl_ctx, &done);
                QLOGD("...done=%d", done);
                if (ret == 0 && done == QOSA_TRUE)
                {
                    QLOGD("ssl_handshark success");
                    qosa_memset(buffer, 0, SEND_BUFF_MAX_LEN + 1);
                    qosa_strcpy(buffer, GET_REQUEST);
                    QLOGD("...buffer=%s", buffer);
                    ret = qcm_ssl_write(ssl_ctx, buffer, qosa_strlen(buffer), &result_code);
                    if (ret < 0 && result_code == QCM_VTLS_SSL_CONNECT_ERR)
                    {
                        QLOGE("write error ret=%x", ret);
                        goto exit;
                    }
                }
                else if (ret == 0 && done == QOSA_FALSE)
                {
                    QLOGD("ssl connect continue...");
                }
                else
                {
                    QLOGE("ssl connect error=%d", ret);
                    break;
                }
            }
            else
            {
                // SSL connection successful, read data sent by SSL server
                qosa_memset(buffer, 0, SEND_BUFF_MAX_LEN + 1);
                ret = qcm_ssl_read(ssl_ctx, buffer, SEND_BUFF_MAX_LEN, &result_code);
                QLOGD("ret=%x result_code=%x", ret, result_code);
                if (ret < 0 && result_code == QCM_VTLS_SSL_CONNECT_ERR)
                {
                    QLOGE("read error ret=%d", ret);
                    goto exit;
                }
                QLOGV("%s", buffer);
            }
        }
    }

exit:
    QLOGV("exit");
    qcm_ssl_close(ssl_ctx);
    qcm_ssl_free(ssl_ctx);
    close(client_socket);
    return;
}

/**
 * @brief Verify SSL connection using non-blocking socket for non-blocking SSL interface connection validation
 */
void unir_vtls_noblock_demo2_init(void)
{
    int         err = 0;
    qosa_task_t ssl_noblock_task = QOSA_NULL;

    err = qosa_task_create(
        &ssl_noblock_task,
        VTLS_NOBLOCK_DEMO2_TASK_STACK_SIZE,
        QOSA_PRIORITY_NORMAL,
        "ssl_noblock2",
        unir_ssl_noblock_demo2_thread,
        QOSA_NULL
    );
    if (err != QOSA_OK)
    {
        QLOGE("task create error");
        return;
    }
}
UNIRTOS_APP_EXPORT(323, "vtls_noblock_demo2", unir_vtls_noblock_demo2_init);
