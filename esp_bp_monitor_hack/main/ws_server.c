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
#include "sys/param.h"
#include <esp_app_format.h>
#include "spiffs.h"

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

static esp_err_t otaw2_update_handler(httpd_req_t *req)
{
    size_t total_len = req->content_len;
    size_t received = 0;
    uint8_t pct_received = 0;
    char buf[BUF_SIZE];
    int total = 0, ret;
    esp_err_t err;
    const char *part_q = httpd_req_get_url_query_str(req, buf, sizeof(buf))
                             ? buf
                             : "";

    // Determine which partition to update
    char part_name[PART_ARG_LEN] = {0};
    httpd_query_key_value(part_q, "partition", part_name, PART_ARG_LEN);
    bool is_fw = (strcmp(part_name, "firmware") == 0);

    draw_ota(is_fw ? "OS" : "SPIFFS", 0, "ready");

    // Select the partition
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

    // Start OTA or erase SPIFFS
    esp_ota_handle_t ota_handle = 0;
    size_t header_skipped = 0;
    const size_t header_size = sizeof(esp_image_header_t) +
                               sizeof(esp_image_segment_header_t) +
                               sizeof(esp_app_desc_t);

    esp_image_header_t img_header;
    esp_image_segment_header_t seg_header;
    esp_app_desc_t app_desc;

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
        err = esp_partition_erase_range(update_part, 0, update_part->size);
        if (err != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "SPIFFS erase failed");
            draw_ota(is_fw ? "OS" : "SPIFFS", 0, "SPIFFS erase failed");
            return err;
        }
    }

    // Read the uploaded file in chunks
    while ((ret = httpd_req_recv(req, buf, BUF_SIZE)) > 0)
    {
        if (header_skipped < header_size)
        {
            // Skip the header for both firmware and SPIFFS updates
            size_t to_skip = header_size - header_skipped;
            if (ret <= to_skip)
            {
                header_skipped += ret;
                continue;
            }
            else
            {
                header_skipped = header_size;
                memcpy(&img_header, buf, sizeof(esp_image_header_t));
                memcpy(&seg_header, buf + sizeof(esp_image_header_t), sizeof(esp_image_segment_header_t));
                memcpy(&app_desc, buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(esp_app_desc_t));

                ESP_LOGI(TAG, "App version: %s", app_desc.version);
                ESP_LOGI(TAG, "Project name: %s", app_desc.project_name);

                memmove(buf, buf + to_skip, ret - to_skip);
                ret -= to_skip;
            }
        }

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
        received += ret;
        uint8_t new_pct_received = (uint8_t)((received * 100) / total_len);
        if (pct_received != new_pct_received)
        {
            pct_received = new_pct_received;
            draw_ota(is_fw ? "OS" : "SPIFFS", pct_received, "downloading...");
        }
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
        err = esp_ota_set_boot_partition(update_part);
        if (err != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "esp_ota_set_boot_partition failed");
            draw_ota(is_fw ? "OS" : "SPIFFS", pct_received, "esp_set_boot_part failed");
            return err;
        }
    }

    // Success
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK");
    
    TimerHandle_t reboot_timer = xTimerCreate(
        "reboot_timer",              // name (for debugging)
        pdMS_TO_TICKS(10000),        // period in ticks (10 000 ms)
        pdFALSE,                     // pdFALSE = one‐shot, pdTRUE = auto‐reload
        NULL,                        // timer “ID” (not needed here)
        reboot_timer_cb             // callback
    );
    ESP_LOGE(TAG, "OTA complete");

    if (xTimerStart(reboot_timer, /*ticks to wait*/ 100) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start reboot timer");
    }

    return ESP_OK;
}

static esp_err_t ota_os(httpd_req_t *req)
{
    size_t total_len = req->content_len;
    uint8_t pct_received = 0;
    esp_err_t err = ESP_OK;

    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "Starting OTA OS");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08"PRIx32", but running from offset 0x%08"PRIx32,
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08"PRIx32")",
             running->type, running->subtype, running->address);

    update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition != NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%"PRIx32,
             update_partition->subtype, update_partition->address);

    int binary_file_length = 0;

    char *buf = malloc(BUF_SIZE);

    /* Content length of the request gives
     * the size of the file being uploaded */
    int remaining = total_len;

    /*deal with all receive packet*/
    bool image_header_was_checked = false;
    while (remaining > 0) {
        int data_read = httpd_req_recv(req, buf, BUF_SIZE);
        //ESP_LOGI(TAG, "Remaining: %d", remaining);
        uint8_t new_pct_received = (uint8_t)(((total_len - remaining) * 100) / total_len);
        if (pct_received != new_pct_received)
        {
            pct_received = new_pct_received;
            draw_ota("OS", pct_received, "downloading...");
        }
        if (data_read <= 0) {
            if (data_read == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry if timeout occurred */
                continue;
            }

            ESP_LOGE(TAG, "OTA reception failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive ota");
            return ESP_FAIL;
        } else {
            if (image_header_was_checked == false) {
                esp_app_desc_t new_app_info;
                if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                    // check current version with downloading
                    memcpy(&new_app_info, &buf[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                    ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                    esp_app_desc_t running_app_info;
                    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
                    }

                    const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
                    esp_app_desc_t invalid_app_info;
                    if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
                    }

                    // check current version with last invalid partition
                    if (last_invalid_app != NULL) {
                        if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) {
                            ESP_LOGW(TAG, "New version is the same as invalid version.");
                            ESP_LOGW(TAG, "Previously, there was an attempt to launch the firmware with %s version, but it failed.", invalid_app_info.version);
                            ESP_LOGW(TAG, "The firmware has been rolled back to the previous version.");
                            return ESP_FAIL;
                        }
                    }

                    image_header_was_checked = true;

                    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                        esp_ota_abort(update_handle);
                        return ESP_FAIL;
                    }
                    ESP_LOGI(TAG, "esp_ota_begin succeeded");
                } else {
                    ESP_LOGE(TAG, "received package is not fit len");
                    esp_ota_abort(update_handle);
                    return ESP_FAIL;
                }
            }
            err = esp_ota_write( update_handle, (const void *)buf, data_read);
            if (err != ESP_OK) {
                esp_ota_abort(update_handle);
                return ESP_FAIL;
            }
            binary_file_length += data_read;
            ESP_LOGD(TAG, "Written image length %d", binary_file_length);
        }
        remaining -= data_read;
    }

    ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

static esp_err_t ota_spiffs(httpd_req_t *req){
    size_t total_len = req->content_len;
    uint8_t pct_received = 0;
    esp_err_t err = ESP_OK;
    size_t dst_offset = 0;
    const esp_partition_t *update_part = NULL;
    update_part = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,
    ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
    "storage");
    if (update_part == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Partition not found");
        draw_ota("SPIFFS", 0, "Partition not found");
        return ESP_FAIL;
    }
    err = esp_partition_erase_range(update_part, 0, update_part->size);
    if (err != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "SPIFFS erase failed");
        draw_ota("SPIFFS", 0, "SPIFFS erase failed");
        return err;
    }

    int binary_file_length = 0;

    char *buf = malloc(BUF_SIZE);

    /* Content length of the request gives
     * the size of the file being uploaded */
    int remaining = total_len;

    /*deal with all receive packet*/
    bool image_header_was_checked = false;
    while (remaining > 0) {
        int data_read = httpd_req_recv(req, buf, BUF_SIZE);
        //ESP_LOGI(TAG, "Remaining: %d", remaining);
        uint8_t new_pct_received = (uint8_t)(((total_len - remaining) * 100) / total_len);
        if (pct_received != new_pct_received)
        {
            pct_received = new_pct_received;
            draw_ota("SPIFFS", pct_received, "downloading...");
        }
        if (data_read <= 0) {
            if (data_read == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry if timeout occurred */
                continue;
            }

            ESP_LOGE(TAG, "OTA reception failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive ota");
            return ESP_FAIL;
        } else {
            if (image_header_was_checked == false) {
                esp_app_desc_t new_app_info;
                if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                    // check current version with downloading
                    memcpy(&new_app_info, &buf[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                    ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                    const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
                    esp_app_desc_t invalid_app_info;
                    if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
                    }

                    // check current version with last invalid partition
                    if (last_invalid_app != NULL) {
                        if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) {
                            ESP_LOGW(TAG, "New version is the same as invalid version.");
                            ESP_LOGW(TAG, "Previously, there was an attempt to launch the firmware with %s version, but it failed.", invalid_app_info.version);
                            ESP_LOGW(TAG, "The firmware has been rolled back to the previous version.");
                            return ESP_FAIL;
                        }
                    }

                    image_header_was_checked = true;

                } else {
                    ESP_LOGE(TAG, "received package is not fit len");
                    return ESP_FAIL;
                }
            }
            err = esp_partition_write(update_part, dst_offset, (const void *)buf, data_read);
            dst_offset += data_read;
            if (err != ESP_OK) {
                return ESP_FAIL;
            }
            binary_file_length += data_read;
            ESP_LOGD(TAG, "Written image length %d", binary_file_length);
        }
        remaining -= data_read;
    }

    ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);

    return err;
}

static esp_err_t ota_update_handler(httpd_req_t *req)
{

    size_t total_len = req->content_len;
    size_t query_len = httpd_req_get_url_query_len(req);
    ESP_LOGE(TAG, "URL query length: %d", query_len);
    char *buf_url_q = malloc((query_len + 1) * sizeof(char));
    esp_err_t err = httpd_req_get_url_query_str(req, buf_url_q, query_len+1);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get URL query string");
        ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
        free(buf_url_q);
        return ESP_FAIL;
    }
    ESP_LOGE(TAG, "URL query: %s", buf_url_q);

    // Determine which partition to update
    char part_name[PART_ARG_LEN] = {0};
    httpd_query_key_value(buf_url_q, "partition", part_name, PART_ARG_LEN);
    ESP_LOGE(TAG, "Partition: %s", part_name);
    bool is_fw = (strcmp(part_name, "firmware") == 0);

    draw_ota(is_fw ? "OS" : "SPIFFS", 0, "ready");
    
    if (is_fw)
    {
        err = ota_os(req);
    }
    else
    {
        err = ota_spiffs(req);
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(err));
        draw_ota(is_fw ? "OS" : "SPIFFS", 0, "OTA update failed");
        free(buf_url_q);
        return err;
    }
    
    ESP_LOGI(TAG, "Prepare to restart system!");
   
    TimerHandle_t reboot_timer = xTimerCreate(
        "reboot_timer",              // name (for debugging)
        pdMS_TO_TICKS(2000),        // period in ticks (10 000 ms)
        pdFALSE,                     // pdFALSE = one‐shot, pdTRUE = auto‐reload
        NULL,                        // timer “ID” (not needed here)
        reboot_timer_cb             // callback
    );
    ESP_LOGE(TAG, "OTA complete");

    if (xTimerStart(reboot_timer, /*ticks to wait*/ 100) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start reboot timer");
    }
    
    draw_ota(is_fw ? "OS" : "SPIFFS", 100, "Rebooting now ...");
    
    // Success
    httpd_resp_set_status(req, "200");
    httpd_resp_set_type(req, "text/plain");                // optional
    httpd_resp_set_hdr(req, "Content-Length", "0");        // explicit zero-length
    httpd_resp_set_hdr(req, "Connection", "close");        // signal connection close
    // send an empty string (zero bytes) – curl sees Content-Length=0 and closes
    httpd_resp_send(req, "", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static void reboot_timer_cb(TimerHandle_t xTimer)
{
    
    ESP_LOGE(TAG, "Restarting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart(); 
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

    //config.stack_size = 8192;

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
