/* WebSocket Echo Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_http_server.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include "ws_server.h"
#include "bpm_handler.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "display.h"

static esp_err_t trigger_async_send(httpd_handle_t handle, int fd, const char *msg);

static const char *TAG = "ws_server";

#define MAX_WS_CLIENTS 5
static int ws_clients[MAX_WS_CLIENTS] = {-1};

static void add_ws_client(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; ++i)
    {
        if (ws_clients[i] == -1 || ws_clients[i] == NULL)
        {
            ws_clients[i] = fd;
            break;
        }
    }
}

static void remove_ws_client(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; ++i)
    {
        if (ws_clients[i] == fd)
        {
            ws_clients[i] = -1;
            break;
        }
    }
}

void ws_send_msg_broadcast(char *msg, httpd_handle_t *server)
{
    for (int i = 0; i < MAX_WS_CLIENTS; ++i)
    {
        if (ws_clients[i] != -1 && ws_clients[i] != NULL)
        {
            esp_err_t err = trigger_async_send(server, ws_clients[i], msg);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to send to client %d, removing", ws_clients[i]);
                remove_ws_client(ws_clients[i]);
            }
        }
    }
}

static void ws_help_broadcast_task(void *arg)
{
    const char *msg = "help";

    while (1)
    {
        ws_send_msg_broadcast(msg, (httpd_handle_t)arg);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/*
 * Structure holding server handle
 * and internal socket fd in order
 * to use out of request send
 */
struct async_resp_arg
{
    httpd_handle_t hd;
    int fd;
    char *msg;
};

/*
 * async send function, which we put into the httpd work queue
 */
static void ws_async_send(void *arg)
{
    struct async_resp_arg *resp_arg = arg;
    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)resp_arg->msg,
        .len = strlen(resp_arg->msg)};

    httpd_ws_send_frame_async(resp_arg->hd, resp_arg->fd, &ws_pkt);

    free(resp_arg->msg); // free copied message
    free(resp_arg);      // free struct
}

static esp_err_t trigger_async_send(httpd_handle_t handle, int fd, const char *msg)
{
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    if (!resp_arg)
        return ESP_ERR_NO_MEM;

    resp_arg->hd = handle;
    resp_arg->fd = fd;
    resp_arg->msg = strdup(msg);
    if (!resp_arg->msg)
    {
        free(resp_arg);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = httpd_queue_work(handle, ws_async_send, resp_arg);
    if (ret != ESP_OK)
    {
        free(resp_arg->msg);
        free(resp_arg);
    }
    return ret;
}

esp_err_t index_handler(httpd_req_t *req)
{
    // 1) Try to open "/spiffs/index.html" for reading
    FILE *file = fopen("/spiffs/index.html", "r");
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open /spiffs/index.html");
        // Send a 404 response if the file is missing
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    // 2) Inform the client that we're sending back HTML
    httpd_resp_set_type(req, "text/html");

    // 3) Read and send the file in reasonably‐sized chunks
    char buffer[1024];
    size_t read_bytes;
    do
    {
        read_bytes = fread(buffer, 1, sizeof(buffer), file);
        if (read_bytes > 0)
        {
            // Send exactly 'read_bytes' to the HTTP client.
            // If this fails in the middle, abort and clean up.
            if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK)
            {
                ESP_LOGE(TAG, "Error sending chunk to client");
                fclose(file);
                // Send zero‐length chunk to indicate “end” (so the client doesn’t hang)
                httpd_resp_send_chunk(req, NULL, 0);
                return ESP_FAIL;
            }
        }
    } while (read_bytes == sizeof(buffer));
    // When fread() returns < sizeof(buffer), we’re at EOF (or error).
    // If it's an error, we still send whatever we have and then close.

    // 4) Signal end‐of‐response by sending a zero‐length chunk
    httpd_resp_send_chunk(req, NULL, 0);

    // 5) Clean up the FILE handle
    fclose(file);
    return ESP_OK;
}

esp_err_t handle_ws_msg(char *msg_rec, httpd_handle_t *server, int ws_client)
{
    ESP_LOGE(TAG, "MSG: %s", msg_rec);
    if (strcmp(msg_rec, "start") == 0)
    {
        // Message is "start"
        ESP_LOGE(TAG, "HEYHEYHEY");
        start_measurement();
        char *msg = "hehehehehehehe";
        return trigger_async_send(*server, ws_client, msg);
    }
    return ESP_OK;
}

/*
 * This handler echos back the received ws data
 * and triggers an async send if certain message received
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, new connection: fd=%d", httpd_req_to_sockfd(req));
        add_ws_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
    if (ws_pkt.len)
    {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        // Print received msg from ws
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }
    ESP_LOGI(TAG, "Packet type: %d", ws_pkt.type);
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT)
    {
        esp_err_t err = handle_ws_msg((char *)ws_pkt.payload, &req->handle, httpd_req_to_sockfd(req));
        free(buf);
        return err;
    }

    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "websocket receive failed, removing fd %d", httpd_req_to_sockfd(req));
        remove_ws_client(httpd_req_to_sockfd(req));
        return ret;
    }
    free(buf);
    return ret;
}

#define BUF_SIZE 1024
#define PART_ARG_LEN 16

static esp_err_t ota_update_handler(httpd_req_t *req)
{
    size_t total_len = req->content_len;
    size_t received = 0;
    int pct_received = 0;
    char buf[BUF_SIZE];
    int total = 0, ret;
    esp_err_t err;
    const char *part_q = httpd_req_get_url_query_str(req, buf, sizeof(buf))
                             ? buf
                             : "";

    // figure out which partition
    char part_name[PART_ARG_LEN] = {0};
    httpd_query_key_value(part_q, "partition", part_name, PART_ARG_LEN);
    bool is_fw = (strcmp(part_name, "firmware") == 0);

    draw_ota(is_fw ? "OS" : "SPIFFS", 0, "ready");

    // select partition
    const esp_partition_t *update_part = NULL;
    if (is_fw)
    {
        update_part = esp_ota_get_next_update_partition(NULL);
    }
    else
    {
        update_part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
            "storage");
    }
    if (update_part == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Partition not found");
        draw_ota(is_fw ? "OS" : "SPIFFS", 0, "Partition not found");
        return ESP_FAIL;
    }

    // start OTA (or erase SPIFFS)
    esp_ota_handle_t ota_handle = 0;
    if (is_fw)
    {
        err = esp_ota_begin(update_part, OTA_SIZE_UNKNOWN, &ota_handle);
        if (err != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "esp_ota_begin failed");
            draw_ota(is_fw ? "OS" : "SPIFFS", 0, "esp_ota_begin failed");
            return err;
        }
    }
    else
    {
        // erase entire SPIFFS partition
        err = esp_partition_erase_range(update_part, 0, update_part->size);
        if (err != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "SPIFFS erase failed");
            draw_ota(is_fw ? "OS" : "SPIFFS", 0, "SPIFFS erase failed");
            return err;
        }
    }

    // read the uploaded file in chunks
    while ((ret = httpd_req_recv(req, buf, BUF_SIZE)) > 0)
    {
        if (is_fw)
        {
            err = esp_ota_write(ota_handle, (const void *)buf, ret);
            if (err != ESP_OK)
            {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "esp_ota_write failed");
                draw_ota(is_fw ? "OS" : "SPIFFS", pct_received, "esp_ota_write failed");
                esp_ota_end(ota_handle);
                return err;
            }
        }
        else
        {
            // write raw to SPIFFS partition
            err = esp_partition_write(update_part, total, buf, ret);
            if (err != ESP_OK)
            {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "SPIFFS write failed");
                draw_ota(is_fw ? "OS" : "SPIFFS", pct_received, "SPIFFS write failed");
                return err;
            }
        }
        total += ret;
        pct_received = (int)((received * 100) / total_len);
        draw_ota(is_fw ? "OS" : "SPIFFS", pct_received, "downloading...");
    }
    if (ret < 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Failed to receive file");
        draw_ota(is_fw ? "OS" : "SPIFFS", pct_received, "Failed to receive file");
        return ESP_FAIL;
    }

    if (is_fw)
    {
        err = esp_ota_end(ota_handle);
        if (err != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "esp_ota_end failed");
            draw_ota(is_fw ? "OS" : "SPIFFS", pct_received, "esp_ota_end failed");
            return err;
        }
        // switch boot partition
        err = esp_ota_set_boot_partition(update_part);
        if (err != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "esp_ota_set_boot_partition failed");
            draw_ota(is_fw ? "OS" : "SPIFFS", pct_received, "esp_set_boot_part failed");
            return err;
        }
    }

    // success!
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK");
    draw_ota(is_fw ? "OS" : "SPIFFS", 100, "Rebooting now ...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

static const httpd_uri_t index_site = {
    .uri = "/index.html",
    .method = HTTP_GET,
    .handler = index_handler,
};

static const httpd_uri_t ws = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .user_ctx = NULL,
    .is_websocket = true};

static const httpd_uri_t ota_update = {
    .uri = "/update",
    .method = HTTP_POST,
    .handler = ota_update_handler,
    .user_ctx = NULL};

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Registering the ws handler
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &ws);
        httpd_register_uri_handler(server, &index_site);
        httpd_register_uri_handler(server, &ota_update);

        ESP_LOGI(TAG, "Creating tasks");
        // xTaskCreate(ws_help_broadcast_task, "ws_help_broadcast_task", 4096, server, 5, NULL);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

void disconnect_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server)
    {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK)
        {
            *server = NULL;
        }
        else
        {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

void connect_handler(void *arg, esp_event_base_t event_base,
                     int32_t event_id, void *event_data)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server == NULL)
    {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}
