/* 
    mqtt part adapted from ESP32 examples: Protocols, MQTT over TCP
    freertos queues from: https://github.com/Mair/FreeRtos-meetup/tree/master/_5_7_queues
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "MQTT_EXAMPLE";

xQueueHandle queue;

bool enableProduce = true;

void populateQueue(void *params)
{
    int count = 0;
    while (true)
    {
        if (enableProduce)
        {
            count++;
            printf("producing message\n");
            long ok = xQueueSend(queue, &count, 100 / portTICK_PERIOD_MS);
            if (ok)
            {
                printf("added message to queue\n");
            }
            else
            {
                printf("failed to add message to queue\n");
            }
        }

        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}

void publisher(void *params)
{
    while (true)
    {
        int rxInt;
        if (xQueueReceive(queue, &rxInt, 5000 / portTICK_PERIOD_MS))
        {
            char data[30];
            sprintf(data, "data from esp32: %d", rxInt);

            int msg_id = esp_mqtt_client_publish(params, "/topic/nodered_demo/", data, 0, 2, 1);
            ESP_LOGI(TAG, "published data= %s, msg_id=%d", data, msg_id);

            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "/topic/nodered_demo/", "connected!", 0, 0, 0);
        esp_mqtt_client_subscribe(client, "/topic/nodered_demo/command", 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/nodered_demo/", "subscribed!", 0, 0, 0);
        msg_id = esp_mqtt_client_publish(client, "/topic/nodered_demo/command", "subscribed!", 0, 0, 0);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        // Logic to turn on/off data production, based on message received in the topic COMMAND_TOPIC
        if (strcmp(event->data, "false") == 0)
        {
            printf("data production disabled\n");
            enableProduce = false;
        }
        else if (strcmp(event->data, "true") == 0)
        {
            printf("data production enabled\n");
            enableProduce = true;
        }

        // clean client event data
        memset(event->data, 0, event->data_len);

        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        //.uri = CONFIG_BROKER_URL,
        .host = "165.227.107.127" // our broker running on digital ocean
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);

    xTaskCreate(&publisher, "publish", 2048, client, 1, NULL);
    xTaskCreate(&populateQueue, "populate", 2048, NULL, 1, NULL);
}

void app_main(void)
{
    queue = xQueueCreate(10, sizeof(int));

    ESP_LOGI(TAG, "[APP] Startup..");
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    mqtt_app_start();
}
