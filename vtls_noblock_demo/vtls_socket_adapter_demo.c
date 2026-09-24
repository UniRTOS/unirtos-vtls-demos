/*****************************************************************/ /**
* @file vtls_socket_adapter_demo.c
* @brief
* @author larson.li@quectel.com
* @date 2023-08-31
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
*
* @par EDIT HISTORY FOR MODULE
* <table>
* <tr><th>Date <th>Version <th>Author <th>Description
* <tr><td>2023-08-31 <td>1.0 <td>Larson.Li <td> Init
* </table>
**********************************************************************/
#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qcm_socket_adp.h"
#include "qosa_asyn_dns.h"

#include "qosa_datacall.h"

#include "qosa_buffer_block.h"
#include "qosa_watermark.h"

#include "qcm_vtls.h"
#include "qcm_vtls_cfg.h"
#include "qosa_rtc.h"

#define QOS_LOG_TAG                       LOG_TAG

#define GET_REQUEST                       "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nAccept: */*\r\n\r\n"

#define VTLS_ADAPTER_DEMO_TASK_STACK_SIZE 6144

#define VTLS_ADAPTER_SERVER_PORT          443
#define VTLS_ADAPTER_SERVER_NAME          "www.baidu.com"
#define RECV_BUFF_MAX_LEN                 1500

/** TCP/UDP single send data size, TCP MTU is usually 1350-1460, 1500 can trigger TCP/IP protocol stack packet segmentation */
#define VTLS_ADAPTER_SINGLE_SEND_LEN      1500
/** TCP/UDP single receive data length size */
#define VTLS_ADAPTER_SINGLE_READ_LEN      1500

/** TCP/UDP receive/send Watermark maximum cache space */
#define VTLS_ADAPTER_WATERMARK_LIMIT      10240
#define VTLS_ADAPTER_WATERMARK_LOW_LIMIT  2048
#define VTLS_ADAPTER_WATERMARK_HIGH_LIMIT 8192

#define VTLS_ADAPTER_DEMO_SIMID           0
#define VTLS_ADAPTER_DEMO_PDPID           1
#define VTLS_ADAPTER_DEMO_ACTIVE_TIMEOUT  30  // PDP activation timeout 30s

/**
 * @brief Socket application message type enumeration definition
 *
 * This enumeration defines non-blocking message types used in socket applications,
 * used to identify different socket events and operation requests
 */
typedef enum
{
    SOCKET_APP_ADAPTER_MSG_MIN = 0,       /*!< Message type minimum value */
    SOCKET_APP_ADAPTER_MSG_EVENT_IND = 1, /*!< Socket event notification */
    SOCKET_APP_ADAPTER_MSG_SEND_IND,      /*!< Send data indication */
    SOCKET_APP_ADAPTER_MSG_CLOSE,         /*!< Close socket connection */
    SOCKET_APP_ADAPTER_MSG_MAX            /*!< Message type maximum value */
} socket_app_msg_type_e;

/**
 * @enum socket_app_conn_status_e
 * @brief Socket APP application layer management of socket connection status
 */
typedef enum
{
    SOCKET_APP_CONNECT_STATE_IDLE = 0,                               /*!< Socket not performing any operation */
    SOCKET_APP_CONNECT_STATE_INIT = 1,                               /*!< Socket initialization */
    SOCKET_APP_CONNECT_STATE_CONNECTING = 2,                         /*!< Socket connecting */
    SOCKET_APP_CONNECT_STATE_CONNECT = 3,                            /*!< Socket connection successful */
    SOCKET_APP_CONNECT_STATE_CLOSING = 4,                            /*!< Socket connection closing, preparing to close */
    SOCKET_APP_CONNECT_STATE_SSL_HAND = 5,                           /*!< SSL handshake connection state */
    SOCKET_APP_CONNECT_STATE_CLOSED = SOCKET_APP_CONNECT_STATE_IDLE, /*!< Socket closed and resources released */
} socket_app_conn_status_e;

/**
 * @struct socket_app_msg_info_t
 * @brief Socket event message structure
 */
typedef struct
{
    socket_app_msg_type_e event_type;
    void                 *argv;
} socket_app_msg_info_t;

/**
 * @brief Socket application event indication structure
 *
 * This structure is used to encapsulate socket-related event information, including socket file descriptor,
 * event mask, result code, and user-defined parameters
 */
typedef struct
{
    int   sockfd;      /*!< Socket file descriptor */
    int   event_mask;  /*!< Event mask, identifies the type of event that occurred */
    int   result_code; /*!< Operation result code, indicates the execution result of the operation */
    void *argv;        /*!< User-defined parameter pointer, used to pass additional data */
} socket_app_event_ind_t;

/**
 * @struct socket_app_transfer_session
 * @brief Socket data transfer session structure
 */
typedef struct
{
    qosa_bool_t rx_tx_ready;                       /*!< Watermark initialization flag */

    qosa_bool_t             send_wm_high;          /*!< Send watermark high state flag */
    qosa_buffer_block_t    *current_send_data_ptr; /*!< Current item taken from Watermark */
    qosa_buffer_watermark_t send_wm_ptr;           /*!< Send watermark */
    qosa_q_type_t           send_q;                /*!< Watermark internal management queue */

    qosa_bool_t             recv_wm_high;          /*!< Receive watermark high state flag */
    qosa_buffer_block_t    *current_recv_data_ptr; /*!< Current item taken from Watermark */
    qosa_buffer_watermark_t recv_wm_ptr;           /*!< Receive watermark */
    qosa_q_type_t           recv_q;                /*!< Watermark internal management queue */
} socket_app_transfer_session;

/**
 * @struct socket_app_context_t
 * @brief Socket APP context structure
 */
typedef struct
{
    char                        hostname[256];      /*!< Remote host address */
    int                         hostport;           /*!< Remote host port number */
    int                         sockfd;             /*!< Internally created socket handle information */
    socket_app_conn_status_e    connect_state;      /*!< Socket connection state */
    int                         send_release_count; /*!< Count of send release events */
    socket_app_transfer_session transfer_session;   /*!< Socket app data transfer watermark */
    qosa_bool_t                 already_cfg;        /*!< Whether configuration is already completed */
    qosa_bool_t                 ssl_enable;         /*!< TLS function enable */
    qcm_ssl_connect_data_t     *ssl_ctx;
} socket_app_context_t;

/**
 * @brief Socket event message queue
 */
static qosa_msgq_t g_socket_msgq;
/**
 * @brief Socket APP context
 */
static socket_app_context_t g_socket_context;

static void socket_app_soc_close(socket_app_context_t *context);
static void socket_app_send_data(char *data, int data_len);

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
static int unir_vtls_adapter_datacall_active(void)
{
    /* Define data connection related variables */
    qosa_datacall_conn_t    conn = {0};
    qosa_datacall_ip_info_t info = {0};
    qosa_datacall_errno_e   ret = 0;

    /* Create new data connection object */
    conn = qosa_datacall_conn_new(VTLS_ADAPTER_DEMO_SIMID, VTLS_ADAPTER_DEMO_PDPID, QOSA_DATACALL_CONN_TCPIP);

    /* Check data connection information to determine if PDP is already activated */
    if (QOSA_DATACALL_ERR_NO_ACTIVE == qosa_datacall_get_ip_info(conn, &info))
    {
        QLOGD("sim_id=%d,pdp_id=%d", VTLS_ADAPTER_DEMO_SIMID, VTLS_ADAPTER_DEMO_PDPID);

        /* If PDP is not activated, start synchronous activation process */
        ret = qosa_datacall_start(conn, VTLS_ADAPTER_DEMO_ACTIVE_TIMEOUT);
        if (QOSA_DATACALL_OK != ret)
        {
            QLOGE("datacall ret=%x", ret);
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Send socket application event report to message queue
 *
 * @param cmd Event type command code
 * @param argv Pointer to event-related parameters
 *
 * This function is used to report socket application layer events through the message queue,
 * mainly used for inter-thread communication and event processing
 */
static void socket_app_event_report(socket_app_msg_type_e cmd, void *argv)
{
    /* Prepare message information structure */
    socket_app_msg_info_t msg_info = {0};
    int                   ret = 0;

    /* Initialize message structure and fill event information */
    qosa_memset(&msg_info, 0, sizeof(socket_app_msg_info_t));
    msg_info.event_type = cmd;
    msg_info.argv = argv;

    /* Send message to global message queue */
    ret = qosa_msgq_release(g_socket_msgq, sizeof(socket_app_msg_info_t), (qosa_uint8_t *)&msg_info, QOSA_NO_WAIT);
    if (ret != QOSA_OK)
    {
        QLOGE("msg error");
    }
}

/**
 * @brief Register socket event callback function for processing socket events and reporting
 *
 * This function creates an event indication message structure, fills in relevant event information,
 * and reports the event through the socket_app_event_report function
 *
 * @param sockfd Socket file descriptor
 * @param event_mask Event mask, identifies the type of event that occurred
 * @param result Operation result code
 * @param argv Additional parameter pointer
 *
 */
static void socket_app_register_event_cb(int sockfd, int event_mask, int result, void *argv)
{
    /* Create event indication message structure */
    socket_app_event_ind_t *ind_msg = QOSA_NULL;
    ind_msg = qosa_malloc(sizeof(socket_app_event_ind_t));
    if (ind_msg == QOSA_NULL)
    {
        QLOGE("qosa_malloc error");
        return;
    }

    /* Fill event message structure fields */
    ind_msg->sockfd = sockfd;
    ind_msg->event_mask = event_mask;
    ind_msg->result_code = result;
    ind_msg->argv = argv;

    /* Report socket application event */
    socket_app_event_report(SOCKET_APP_ADAPTER_MSG_EVENT_IND, ind_msg);
}

/**
 * @brief TX data send watermark ring buffer remaining count below low watermark level
 *
 * @param[in] qosa_buffer_watermark_t * wm_ptr
 *          - Watermark operation variable
 *
 * @param[in] void * callback_data
 *          - User parameter passed when registering watermark
 */
static void socket_app_tx_wm_low_level_cb(qosa_buffer_watermark_t *wm_ptr, void *callback_data)
{
    QOSA_UNUSED(wm_ptr);
    QLOGV("...");
    // Get socket application context and update send watermark status
    socket_app_context_t *context = (socket_app_context_t *)callback_data;
    context->transfer_session.send_wm_high = QOSA_FALSE;
    // Watermark data is at low watermark level, upper layer should continue sending data
}

/**
 * @brief TX data send watermark ring buffer remaining count above high watermark level
 *
 * @param[in] qosa_buffer_watermark_t * wm_ptr
 *          - Watermark operation variable
 *
 * @param[in] void * callback_data
 *          - User parameter passed when registering watermark
 */
static void socket_app_tx_wm_high_level_cb(qosa_buffer_watermark_t *wm_ptr, void *callback_data)
{
    QOSA_UNUSED(wm_ptr);
    QLOGV("...");
    socket_app_context_t *context = (socket_app_context_t *)callback_data;
    context->transfer_session.send_wm_high = QOSA_TRUE;
    // Current watermark is in high state, upper layer should stop sending data
    // User should add event notification function here to notify sending module to consider abnormal logic or stop sending data, wait for watermark to return to low state before continuing to send
}

/**
 * @brief TX starts receiving data, watermark space changes from 0 data length to greater than 0 data length
 *
 * @param[in] qosa_buffer_watermark_t * wm_ptr
 *          - Watermark operation variable
 *
 * @param[in] void * callback_data
 *          - User parameter passed when registering watermark
 */
static void socket_app_tx_wm_non_empty_cb(qosa_buffer_watermark_t *wm_ptr, void *callback_data)
{
    QOSA_UNUSED(wm_ptr);
    QLOGV("...");
    socket_app_context_t *context = (socket_app_context_t *)callback_data;
    context->send_release_count++;
    socket_app_event_report(SOCKET_APP_ADAPTER_MSG_SEND_IND, callback_data);
}

/**
 * @brief RX data send watermark ring buffer remaining count below low watermark level
 *
 * @param[in] qosa_buffer_watermark_t * wm_ptr
 *          - Watermark operation variable
 *
 * @param[in] void * callback_data
 *          - User parameter passed when registering watermark
 */
static void socket_app_rx_wm_low_level_cb(qosa_buffer_watermark_t *wm_ptr, void *callback_data)
{
    QOSA_UNUSED(wm_ptr);
    QLOGV("...");
    socket_app_context_t *context = (socket_app_context_t *)callback_data;
    context->transfer_session.send_wm_high = QOSA_FALSE;
    // Watermark data is at low watermark level, upper layer should continue sending data
}

/**
 * @brief RX data send watermark ring buffer remaining count above high watermark level
 *
 * @param[in] qosa_buffer_watermark_t * wm_ptr
 *          - Watermark operation variable
 *
 * @param[in] void * callback_data
 *          - User parameter passed when registering watermark
 */
static void socket_app_rx_wm_high_level_cb(qosa_buffer_watermark_t *wm_ptr, void *callback_data)
{
    QOSA_UNUSED(wm_ptr);
    QLOGV("...");
    socket_app_context_t *context = (socket_app_context_t *)callback_data;
    context->transfer_session.send_wm_high = QOSA_TRUE;
    // Current watermark is in high state, upper layer should stop sending data
    // User should add event notification function here to notify sending module to consider abnormal logic or stop sending data, wait for watermark to return to low state before continuing to send
}

/**
 * @brief RX starts receiving data, watermark space changes from 0 data length to greater than 0 data length
 *
 * @param[in] qosa_buffer_watermark_t * wm_ptr
 *          - Watermark operation variable
 *
 * @param[in] void * callback_data
 *          - User parameter passed when registering watermark
 */
static void socket_app_rx_wm_non_empty_cb(qosa_buffer_watermark_t *wm_ptr, void *callback_data)
{
    QOSA_UNUSED(wm_ptr);
    QOSA_UNUSED(callback_data);
    QLOGV("...");
}

/**
 * @brief Initialize receive and send watermark related parameters for socket application.
 *
 * This function is used to initialize send and receive watermark queues in the transfer session,
 * including setting high and low watermark thresholds, callback function pointers, and statistical counters.
 * Ensures the watermark control mechanism is in correct initial state before data transmission.
 *
 * @param context Pointer to socket application context, containing transfer session information.
 */
static void socket_app_rx_tx_watermark_init(socket_app_context_t *context)
{
    socket_app_transfer_session *transfer_session = QOSA_NULL;

    /* Get transfer session structure pointer and zero it */
    transfer_session = &context->transfer_session;
    qosa_memset(transfer_session, 0, sizeof(socket_app_transfer_session));

    /* Initialize send watermark queue */
    qosa_buffer_queue_init(&transfer_session->send_wm_ptr, VTLS_ADAPTER_WATERMARK_LIMIT, &transfer_session->send_q);
    if (QOSA_FALSE == qosa_buffer_is_wm_empty(&transfer_session->send_wm_ptr))
    {
        qosa_buffer_empty_queue(&transfer_session->send_wm_ptr);
    }

    /* Set basic parameters for send watermark */
    transfer_session->send_wm_ptr.current_bytes = 0;
    transfer_session->send_wm_ptr.low_watermark = VTLS_ADAPTER_WATERMARK_LOW_LIMIT;
    transfer_session->send_wm_ptr.high_watermark = VTLS_ADAPTER_WATERMARK_HIGH_LIMIT;

    /* Set send watermark related callback functions and context data */
    transfer_session->send_wm_ptr.wm_low_water_cb = socket_app_tx_wm_low_level_cb;
    transfer_session->send_wm_ptr.wm_low_water_argv = (void *)context;
    transfer_session->send_wm_ptr.wm_high_water_cb = socket_app_tx_wm_high_level_cb;
    transfer_session->send_wm_ptr.wm_high_water_argv = (void *)context;
    transfer_session->send_wm_ptr.wm_in_queue_cb = QOSA_NULL;
    transfer_session->send_wm_ptr.wm_become_empty_cb = QOSA_NULL;
    transfer_session->send_wm_ptr.wm_become_nonempty_cb = socket_app_tx_wm_non_empty_cb;
    transfer_session->send_wm_ptr.wm_become_nonempty_argv = (void *)context;

    /* Initialize receive watermark queue */
    qosa_buffer_queue_init(&transfer_session->recv_wm_ptr, VTLS_ADAPTER_WATERMARK_LIMIT, &transfer_session->recv_q);
    if (QOSA_FALSE == qosa_buffer_is_wm_empty(&transfer_session->recv_wm_ptr))
    {
        qosa_buffer_empty_queue(&transfer_session->recv_wm_ptr);
    }

    /* Set basic parameters for receive watermark */
    transfer_session->recv_wm_ptr.current_bytes = 0;
    transfer_session->recv_wm_ptr.low_watermark = VTLS_ADAPTER_WATERMARK_LOW_LIMIT;
    transfer_session->recv_wm_ptr.high_watermark = VTLS_ADAPTER_WATERMARK_HIGH_LIMIT;

    /* Set receive watermark related callback functions and context data */
    transfer_session->recv_wm_ptr.wm_low_water_cb = socket_app_rx_wm_low_level_cb;
    transfer_session->recv_wm_ptr.wm_low_water_argv = (void *)context;
    transfer_session->recv_wm_ptr.wm_high_water_cb = socket_app_rx_wm_high_level_cb;
    transfer_session->recv_wm_ptr.wm_high_water_argv = (void *)context;
    transfer_session->recv_wm_ptr.wm_in_queue_cb = QOSA_NULL;
    transfer_session->recv_wm_ptr.wm_become_empty_cb = QOSA_NULL;
    transfer_session->recv_wm_ptr.wm_become_nonempty_cb = socket_app_rx_wm_non_empty_cb;
    transfer_session->recv_wm_ptr.wm_become_nonempty_argv = (void *)context;

    /* Mark receive and send watermarks as ready */
    transfer_session->rx_tx_ready = QOSA_TRUE;
}

/**
 * @brief Socket data processing watermark deinitialization, release data
 *
 * @param[in] socket_app_context_t * context
 *         - Socket context
 */
static void socket_app_rx_tx_watermark_deinit(socket_app_context_t *context)
{
    socket_app_transfer_session *transfer_session = QOSA_NULL;

    transfer_session = &context->transfer_session;
    if (transfer_session->rx_tx_ready == QOSA_FALSE)
    {
        return;
    }
    if (transfer_session->send_wm_ptr.q_ptr != QOSA_NULL)
    {
        if (transfer_session->send_wm_ptr.current_bytes != 0)
        {
            qosa_buffer_empty_queue(&transfer_session->send_wm_ptr);
        }
        qosa_buffer_queue_destroy(&transfer_session->send_wm_ptr);
    }

    if (transfer_session->current_send_data_ptr != QOSA_NULL)
    {
        qosa_buffer_free_packet(&transfer_session->current_send_data_ptr);
        transfer_session->current_send_data_ptr = QOSA_NULL;
    }

    if (transfer_session->recv_wm_ptr.q_ptr != QOSA_NULL)
    {
        if (transfer_session->recv_wm_ptr.current_bytes != 0)
        {
            qosa_buffer_empty_queue(&transfer_session->recv_wm_ptr);
        }
        qosa_buffer_queue_destroy(&transfer_session->recv_wm_ptr);
    }

    if (transfer_session->current_recv_data_ptr != QOSA_NULL)
    {
        qosa_buffer_free_packet(&transfer_session->current_recv_data_ptr);
        transfer_session->current_recv_data_ptr = QOSA_NULL;
    }

    transfer_session->rx_tx_ready = QOSA_FALSE;
}

/**
 * @brief Initialize and establish SSL session connection
 *
 * This function is used to configure SSL parameters for client socket connection and complete SSL handshake process.
 * If SSL configuration is not yet completed, perform initial configuration; then attempt SSL connection,
 * if SSL handshake is not completed, return waiting state, otherwise update connection state and send data.
 *
 * @param context Pointer to socket application context, containing socket file descriptor and connection status information
 * @return qcm_sock_err_code
 *         - QCM_SOCK_SUCCESS: SSL connection successful
 *         - QCM_SOCK_WODBLOCK: SSL handshake not completed, need to continue waiting
 *         - QCM_SOCK_SSL_CONN_ERROR: SSL connection failed
 */
static qcm_sock_err_code socket_client_ssl_session_setup(socket_app_context_t *context)
{
    qcm_sock_err_code result = QCM_SOCK_SUCCESS;

    // If SSL is not yet configured, perform SSL configuration
    if (context->already_cfg == QOSA_FALSE)
    {
        qcm_ssl_config_t ssl_config = {0};  // SSL connection configuration structure

        // Initialize SSL configuration structure
        qosa_memset(&ssl_config, 0x00, sizeof(qcm_ssl_config_t));
        ssl_config.ssl_version = QCM_SSL_VERSION_ALL;  // Support up to TLS 1.2 version
        ssl_config.auth_mode = QCM_SSL_VERIFY_NULL;    // Do not verify server certificate
        ssl_config.transport = QCM_SSL_TLS_PROTOCOL;   // Use TLS protocol for transmission
        ssl_config.socket_fd = context->sockfd;        // Set socket file descriptor
        ssl_config.ssl_negotiate_timeout = 30;         // SSL negotiation timeout 30 seconds
        ssl_config.sni_enable = QOSA_TRUE;             // Enable SNI support
        ssl_config.ssl_log_debug = QOSA_TRUE;          // Enable SSL debug log output

        // Apply SSL configuration and initialize SSL connection handle
        result = qcm_socket_ssl_config(context->sockfd, &ssl_config, VTLS_ADAPTER_SERVER_NAME, QOSA_FALSE);
        if (result == QCM_SOCK_SUCCESS)
        {
            context->already_cfg = QOSA_TRUE;  // Mark SSL as configured
        }
    }

    // If SSL configuration is successful, attempt to establish SSL connection
    if (result == QCM_SOCK_SUCCESS)
    {
        result = qcm_socket_ssl_connect(context->sockfd);
        if (result != QCM_SOCK_SUCCESS)
        {
            // Determine if it's a non-blocking SSL handshake not completed case
            if (result == QCM_SOCK_WODBLOCK)
            {
                context->connect_state = SOCKET_APP_CONNECT_STATE_SSL_HAND;
                QLOGV("ssl_handshark continue!!!");  // SSL handshake continuing
                result = QCM_SOCK_WODBLOCK;
                return result;
            }
            else
            {
                QLOGE("ssl_handshark error!!!");  // SSL handshake error
            }
            result = QCM_SOCK_SSL_CONN_ERROR;     // Set SSL connection error state
        }
        else
        {
            context->connect_state = SOCKET_APP_CONNECT_STATE_CONNECT;
            QLOGV("ssl_handshark success!!!");                            // SSL handshake successful
            socket_app_send_data(GET_REQUEST, qosa_strlen(GET_REQUEST));  // Send GET request data
            result = QCM_SOCK_SUCCESS;
        }
    }

    // If final connection fails, close socket resources
    if (result != QCM_SOCK_SUCCESS)
    {
        // TODO: Handle status reporting
        socket_app_soc_close(context);
    }

    return result;
}

/**
 * @brief Socket close function
 *
 * When socket connection encounters an exception, it needs to actively close the socket connection state.
 * Although this function executes in the same task context, directly closing the socket connection here
 * would make the socket task internal state machine difficult to manage, so here we notify the socket task
 * to perform socket close operation by sending a message.
 *
 * @param[in] socket_app_context_t * context
 *          - Socket context
 */
static void socket_app_soc_close(socket_app_context_t *context)
{
    context->connect_state = SOCKET_APP_CONNECT_STATE_CLOSING;
    socket_app_event_report(SOCKET_APP_ADAPTER_MSG_CLOSE, context);
}

/**
 * @brief Socket data send function
 *
 * This function is responsible for handling socket data sending logic, including SSL handshake status check,
 * connection status judgment, buffer data reading and packaging, calling underlying send interface to send data,
 * and deciding whether to continue sending or retry based on the sending result.
 *
 * @param[in] context Pointer to socket application context, containing connection status, SSL configuration and transfer session information.
 */
static void socket_app_soc_write(socket_app_context_t *context)
{
    int         pullup_len = 0;              // Data length extracted from memory queue
    int         write_len = 0;               // Total data length expected to be sent this time
    int         send_len = 0;                // Actual data length copied from buffer to send buffer
    int         send_ret = 0;                // Return value of qcm_socket_send call
    qosa_bool_t send_continue = QOSA_FALSE;  // Flag indicating whether to continue sending data
    char       *write_buff = QOSA_NULL;

    socket_app_transfer_session *transfer_session = &context->transfer_session;

    // If SSL is enabled and currently in SSL handshake phase, execute SSL handshake process and return
    if (context->ssl_enable == QOSA_TRUE && context->connect_state == SOCKET_APP_CONNECT_STATE_SSL_HAND)
    {
        socket_client_ssl_session_setup(context);
        return;
    }

    // Check if in connected state, otherwise do not perform send operation
    if (context->connect_state != SOCKET_APP_CONNECT_STATE_CONNECT)
    {
        QLOGV("connect_state != SOCKET_APP_CONNECT_STATE_CONNECT");
        return;
    }

    // If send is not ready, do not send
    if (transfer_session->rx_tx_ready == QOSA_FALSE)
    {
        QLOGE("send_ready_false");
        return;
    }

    // If send watermark is too high (buffer full), pause sending
    if (transfer_session->send_wm_high == QOSA_TRUE)
    {
        QLOGV("send_wm_high");
        return;
    }

    // Calculate the maximum data length that can be sent this time, limited by single maximum send length and data amount waiting to be sent in buffer
    write_len = MIN(
        VTLS_ADAPTER_SINGLE_SEND_LEN,
        (qosa_buffer_queue_cnt(&transfer_session->send_wm_ptr) + qosa_buffer_length_packet(transfer_session->current_send_data_ptr))
    );
    QLOGD("write_len=%d", write_len);

    // If no data to send, return directly
    if (write_len == 0)
        return;

    // Allocate temporary buffer for sending
    write_buff = qosa_malloc(write_len);
    if (write_buff == QOSA_NULL)
        return;

    // Extract data from send buffer and fill into write_buff
    while (transfer_session->current_send_data_ptr != QOSA_NULL
           || (transfer_session->current_send_data_ptr = (qosa_buffer_block_t *)qosa_buffer_dequeue(&transfer_session->send_wm_ptr)) != QOSA_NULL)
    {
        pullup_len = qosa_buffer_pullup(&transfer_session->current_send_data_ptr, write_buff + send_len, write_len);

        write_len -= pullup_len;
        send_len += pullup_len;

        if (write_len == 0)
            break;
    }

    // If no actual data to send, free buffer and return
    if (send_len <= 0)
    {
        qosa_free(write_buff);
        return;
    }

    // Execute actual data send operation
    send_ret = qcm_socket_send(context->sockfd, write_buff, send_len);
    QLOGD("send_ret=%d", send_ret);

    // Handle send failure or partial send situation based on send result
    if (send_ret != send_len)
    {
        if (send_ret < 0)
        {
            // Send completely failed, push data back to buffer
            qosa_buffer_pushdown(&transfer_session->current_send_data_ptr, (void *)write_buff, send_len);
        }
        else
        {
            // Partial send successful, push remaining data back to buffer
            qosa_buffer_pushdown(&transfer_session->current_send_data_ptr, (void *)(write_buff + send_ret), send_len - send_ret);
            // Set flag to trigger subsequent send attempts
            send_continue = QOSA_TRUE;
        }
    }
    else
    {
        // All send successful, set continue send flag
        send_continue = QOSA_TRUE;
    }

    // Free temporary send buffer
    qosa_free(write_buff);

    // If allowed to continue sending and send event queue is not full, trigger send event again
    if (send_continue == QOSA_TRUE && context->send_release_count < 3)
    {
        socket_app_event_report(SOCKET_APP_ADAPTER_MSG_SEND_IND, context);
    }
}

/**
 * @brief Read data from socket and store to receive queue
 *
 * This function is responsible for reading data from the specified socket connection,
 * performs corresponding processing based on connection status and SSL enable status,
 * and stores the read data into the receive queue for subsequent processing.
 *
 * @param context Socket application context pointer, containing connection status, socket descriptor and other information
 */
static void socket_app_soc_read(socket_app_context_t *context)
{
    int                  ret = 0;
    char                 recv_buff[RECV_BUFF_MAX_LEN] = {0};
    qosa_buffer_block_t *read_data_item = QOSA_NULL;

    socket_app_transfer_session *transfer_session = &context->transfer_session;

    // If SSL is enabled and currently in SSL handshake state, perform SSL session setup
    if (context->ssl_enable == QOSA_TRUE && context->connect_state == SOCKET_APP_CONNECT_STATE_SSL_HAND)
    {
        socket_client_ssl_session_setup(context);
        return;
    }

    // Check if connection status is connected state, if not return directly
    if (context->connect_state != SOCKET_APP_CONNECT_STATE_CONNECT)
    {
        QLOGV("connect_state != SOCKET_APP_CONNECT_STATE_CONNECT");
        return;
    }

    // Check if receive watermark flag is high, if yes return directly
    if (transfer_session->recv_wm_high == QOSA_TRUE)
    {
        QLOGV("recv_wm_high");
        return;
    }

    // Read data from socket
    ret = qcm_socket_read(context->sockfd, recv_buff, RECV_BUFF_MAX_LEN);
    if (ret > 0)
    {
        // Encapsulate the read data into memory item and add to queue tail
        qosa_buffer_pushdown_tail(&read_data_item, (void *)recv_buff, ret);
        QLOGD("ret=%d recv_buff=%s", ret, recv_buff);
    }

    // If read function returns error or length is 0, return directly and do not continue processing

    // If data is successfully read, add data item to receive queue
    if (read_data_item != QOSA_NULL)
    {
        qosa_buffer_enqueue(&transfer_session->recv_wm_ptr, &read_data_item);
        // TODO: If event notification is needed for each data read, add event notification function here
    }
}

/**
 * @brief Socket active notification event processing function
 */
/**
 * @brief Callback function for processing socket application events
 *
 * This function handles socket connection, read, write, close and other operations based on different event types.
 * Supports processing flow for both regular sockets and SSL encrypted sockets.
 *
 * @param argv Pointer to event indication message, containing event type and related parameters
 */
static void socket_app_event_process(void *argv)
{
    // Get event indication message and application context
    socket_app_event_ind_t *ind_msg = (socket_app_event_ind_t *)argv;
    socket_app_context_t   *context = (socket_app_context_t *)ind_msg->argv;

    // Process different types of socket events based on event mask
    switch (ind_msg->event_mask)
    {
        // Process socket connection event
        case QCM_SOCK_CONNECT_EVENT:
            QLOGV("QCM_SOCK_CONNECT_EVENT");
            if (ind_msg->result_code == QCM_SOCK_SUCCESS)
            {
                QLOGD("socketfd=%d connect success", ind_msg->sockfd);
                // If SSL is enabled, set up SSL session
                if (context->ssl_enable == QOSA_TRUE)
                {
                    socket_client_ssl_session_setup(context);
                    return;
                }
                else
                {
                    // Update connection status to connected
                    context->connect_state = SOCKET_APP_CONNECT_STATE_CONNECT;
                }
            }
            else
            {
                QLOGE("socketfd=%d connect error", ind_msg->sockfd);
            }
            break;
        // Process socket read event
        case QCM_SOCK_READ_EVENT:
            QLOGV("QCM_SOCK_READ_EVENT");
            socket_app_soc_read(context);
            break;
        // Process socket write event
        case QCM_SOCK_WRITE_EVENT:
            QLOGV("QCM_SOCK_WRITE_EVENT");
            socket_app_soc_write(context);
            break;
        // Process socket close event
        case QCM_SOCK_CLOSE_EVENT:
            QLOGV("QCM_SOCK_CLOSE_EVENT");
            socket_app_soc_close(context);
            break;
        // Process SSL handshake timeout event
        case QCM_SOCK_SSL_HD_TIMEOUT_EVENT:
            QLOGV("QCM_SOCK_SSL_HD_TIMEOUT_EVENT");
            socket_app_soc_close(context);
            break;
        default:
            break;
    }

    // Free event indication message memory
    qosa_free(ind_msg);
}

/**
 * @brief Create and initialize a TCP socket connection.
 *
 * This function obtains the target host's IP address through DNS resolution, then attempts to create a non-blocking socket,
 * registers event callback functions, and finally initiates a TCP connection request.
 *
 * @param context Pointer to socket application context, containing hostname, port and other information.
 *
 * @return Returns socket file descriptor (>=0) on success, returns negative value on failure, specific information refer to qcm_sock_err_code
 */
static int socket_app_socket_create(socket_app_context_t *context)
{
    int                    sockfd = -1;
    int                    ret = 0;
    struct qosa_addrinfo_s hints, *rp, *result;

    hints.ai_family = QCM_AF_INET;

    // First perform DNS resolution
    if (qosa_dns_syn_getaddrinfo(VTLS_ADAPTER_DEMO_SIMID, VTLS_ADAPTER_DEMO_PDPID, context->hostname, &hints, &result) != QOSA_DNS_RESULT_OK)
    {
        QLOGE("qosa_dns_syn_getaddrinfo error");
        return -1;
    }
    else
    {
        QLOGV("qosa_dns_syn_getaddrinfo success");

        // Traverse DNS resolution results, attempt to establish connection
        for (rp = result; rp != QOSA_NULL; rp = rp->ai_next)
        {
            // Use qcm_socket_create to create non-blocking socket
            sockfd = qcm_socket_create(VTLS_ADAPTER_DEMO_SIMID, VTLS_ADAPTER_DEMO_PDPID, rp->ai_family, QCM_SOCK_STREAM, QCM_TCP_PROTOCOL, 0, QOSA_FALSE);
            if (sockfd < 0)
            {
                QLOGE("qcm_socket_create error");
                return -1;
            }

            // Register event callback function
            ret = qcm_socket_register_event(
                sockfd,
                QCM_SOCK_WRITE_EVENT | QCM_SOCK_READ_EVENT | QCM_SOCK_CLOSE_EVENT | QCM_SOCK_CONNECT_EVENT,
                socket_app_register_event_cb,
                context
            );

            QLOGV("ip--->%s port=%d", rp->ip_addr, context->hostport);

            qosa_ip_addr_t ip_addr;
            // Convert IP address string to network byte order
            inet_pton(AF_INET, rp->ip_addr, &ip_addr.addr.ipv4_addr);
            ip_addr.ip_vsn = QOSA_PDP_IPV4;

            // Use non-blocking method to establish TCP connection
            ret = qcm_socket_connect(sockfd, &ip_addr, context->hostport);
            if (ret == 0 || ret == QCM_SOCK_WODBLOCK)
            {
                // Connection successful or in progress, no need to continue trying other IP addresses
                QLOGV("qcm_socket_connect continue");
                break;
            }
            else
            {
                qcm_socket_close(sockfd);
                sockfd = -1;
            }
        }

        // Free DNS resolution result memory
        qosa_dns_result_free(result);
    }

    QLOGD("sockfd=%d", sockfd);
    return sockfd;
}

/**
 * @brief Process socket active data sending
 */
static void socket_app_send_process(void *argv)
{
    socket_app_context_t *context = (socket_app_context_t *)argv;
    context->send_release_count--;
    socket_app_soc_write(context);
}

/**
 * @brief Close socket application connection processing function
 * @param argv Pointer to socket application context
 *
 * This function is responsible for handling the socket application's close process,
 * including setting socket linger option, closing socket connection, and updating connection status.
 */
static void socket_app_close_process(void *argv)
{
    QOSA_UNUSED(argv);
    socket_app_context_t *context = (socket_app_context_t *)argv;
    qcm_socket_linger_t   q_linger;

    // Check current connection status, if in idle state return directly
    if (context->connect_state == SOCKET_APP_CONNECT_STATE_IDLE)
    {
        return;
    }

    // Set socket linger time to 0, close socket immediately
    q_linger.on_off = 1;
    q_linger.linger_val = 0;
    qcm_socket_set_opt(context->sockfd, QCM_SOCK_LINGER_OPT, &q_linger);

    // Close socket and update context status
    qcm_socket_close(context->sockfd);
    context->sockfd = -1;
    context->connect_state = SOCKET_APP_CONNECT_STATE_CLOSED;
}

/**
 * @brief Non-blocking socket thread
 */
static void unir_vtls_adapter_demo_thread(void *argv)
{
    int                   sockfd = -1;
    socket_app_msg_info_t msg_info;
    int                   ret = 0;
    socket_app_context_t *context = &g_socket_context;

    QOSA_UNUSED(argv);
    qosa_task_sleep_sec(10);

    context->connect_state = SOCKET_APP_CONNECT_STATE_INIT;
    qosa_memset(context, 0, sizeof(socket_app_context_t));
    qosa_strcpy(context->hostname, VTLS_ADAPTER_SERVER_NAME);
    context->hostport = VTLS_ADAPTER_SERVER_PORT;

    // Network activation
    ret = unir_vtls_adapter_datacall_active();
    if (ret != 0)
    {
        QLOGE("pdp error");
        return;
    }

    // Create non-blocking socket
    sockfd = socket_app_socket_create(context);
    if (sockfd < 0)
    {
        QLOGE("qcm_socket_connect error");
        return;
    }

    context->connect_state = SOCKET_APP_CONNECT_STATE_CONNECTING;
    context->sockfd = sockfd;  // Enable TLS connection
    context->ssl_enable = QOSA_TRUE;

    // Initialize watermark
    socket_app_rx_tx_watermark_init(context);
    while (1)
    {
        qosa_memset(&msg_info, 0, sizeof(socket_app_msg_info_t));
        ret = qosa_msgq_wait(g_socket_msgq, (qosa_uint8_t *)&msg_info, sizeof(socket_app_msg_info_t), QOSA_WAIT_FOREVER);
        if (ret != QOSA_OK)
        {
            QLOGE("qosa_msgq_wait error");
            continue;
        }

        switch (msg_info.event_type)
        {
            case SOCKET_APP_ADAPTER_MSG_EVENT_IND:
                // Socket event notification
                socket_app_event_process(msg_info.argv);
                break;
            case SOCKET_APP_ADAPTER_MSG_SEND_IND:
                // Process user active data sending
                socket_app_send_process(msg_info.argv);
                break;
            case SOCKET_APP_ADAPTER_MSG_CLOSE:
                // Process user active socket closing
                socket_app_close_process(msg_info.argv);
                goto exit;
            default:
                break;
        }
    }
exit:
    socket_app_rx_tx_watermark_deinit(context);
}

/**
 * @brief Socket active data sending
 * Put the data to be sent into the watermark, after completion the watermark non empty will send event notification to socket task for sending
 *
 * @param data Pointer to the data buffer to be sent
 * @param data_len Length of data to be sent
 */
static void socket_app_send_data(char *data, int data_len)
{
    socket_app_context_t        *context = &g_socket_context;
    socket_app_transfer_session *transfer_session = &context->transfer_session;

    qosa_buffer_block_t *item_ptr = QOSA_NULL;

    item_ptr = qosa_buffer_new_block();
    if (item_ptr == QOSA_NULL)
    {
        QLOGE("qosa_buffer_new_block error");
        return;
    }

    // Put data into watermark
    qosa_buffer_pushdown_tail(&item_ptr, data, data_len);
    qosa_buffer_enqueue(&transfer_session->send_wm_ptr, &item_ptr);
}

/**
 * @brief Socket active close
 */
void socket_app_active_close(void)
{
    socket_app_event_report(SOCKET_APP_ADAPTER_MSG_CLOSE, QOSA_NULL);
}

/**
 * @brief Verify socket connection using non-blocking socket
 */
void unir_vtls_adapter_demo_init(void)
{
    int         err = 0;
    qosa_task_t sock_task = QOSA_NULL;
    err = qosa_msgq_create(&g_socket_msgq, sizeof(socket_app_msg_info_t), 20);
    if (err != QOSA_OK)
    {
        QLOGE("msgq_create error");
        return;
    }

    err = qosa_task_create(&sock_task, VTLS_ADAPTER_DEMO_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "vtls_adapter", unir_vtls_adapter_demo_thread, QOSA_NULL);
    if (err != QOSA_OK)
    {
        qosa_msgq_delete(g_socket_msgq);
        QLOGE("task create error");
        return;
    }
}
