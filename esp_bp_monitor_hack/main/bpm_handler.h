#include "esp_http_server.h"

#ifndef BPM_HANDLER_H
#define BPM_HANDLER_H
/**
 * @brief Structure representing an HTTP server user.
 * 
 * @param server Pointer to the HTTP server handle.
 * @param ws_client WebSocket client identifier.
 * 
 * Usage:
 * Use this structure to manage HTTP server and WebSocket client interactions. Ensure the server and client identifiers are correctly initialized before use.
 */
typedef struct {
    httpd_handle_t *server;
    int ws_client;
} httpd_user_t;

/**
 * @brief Handles the blood pressure measurement process.
 * 
 * This function performs the necessary steps to initiate and manage a blood pressure measurement.
 * It sets up the power for the monitor, performs a self-test, and triggers the measurement process.
 * 
 * @param httpd_user Pointer to the HTTP server user structure containing server and WebSocket client information.
 * 
 * Usage:
 * This function is intended to be run as a task created by start_measurement(). Ensure that the HTTP server and WebSocket client are properly initialized before invoking this function.
 */
static void task_take_measurement(void *param_httpd_user);

/**
 * @brief Starts the blood pressure measurement process.
 *  
 * @param httpd_user Pointer to the HTTP server user structure containing server and WebSocket client information. 
 *
 * Usage:
 * Use this function to create a task that will handle the blood pressure measurement. It checks if a measurement is already in progress and starts a new task if not.
 */
void start_measurement(httpd_user_t *httpd_user);

/**
 * @brief Stops the blood pressure measurement process.
 * 
 * Usage:
 * Use this function to stop the ongoing measurement process. Ensure that the HTTP server is running and the WebSocket client is properly initialized before invoking this function.
 */
void stop_measurement();

#endif