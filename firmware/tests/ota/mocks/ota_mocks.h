#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_TIMEOUT 0x105
#define ESP_ERR_NOT_FOUND 0x106
#define ESP_ERR_NOT_SUPPORTED 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_HTTP_EAGAIN 0x109
#define ESP_ERR_OTA_VALIDATE_FAILED 0x201
#define ESP_ERR_OTA_ALREADY_IN_PROGRESS 0x202
#define ESP_ERR_INVALID_VERSION 0x301
#define ESP_ERR_OTA_SMALL_SEC_VER 0x302
#define ESP_ERR_OTA_SPI_MODE_MISMATCH 0x303
#define ESP_ERR_OTA_PARTITION_CONFLICT 0x304
#define ESP_ERR_OTA_ROLLBACK_FAILED 0x305
#define ESP_ERR_OTA_SELECT_INFO_INVALID 0x306
#define ESP_ERR_HTTPD_INVALID_REQ 0x401
#define ESP_ERR_HTTPD_RESP_SEND 0x402
#define ESP_CHIP_ID_ESP32S3 9
#define ESP_IMAGE_HEADER_MAGIC 0xe9
#define ESP_IMAGE_MAX_SEGMENTS 16
#define ESP_APP_DESC_MAGIC_WORD 0xabcd5432
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_OTA 0
#define ESP_PARTITION_SUBTYPE_APP_OTA_0 0x10
#define ESP_PARTITION_SUBTYPE_APP_OTA_1 0x11
#define OTA_WITH_SEQUENTIAL_WRITES 0
#define CONFIG_LWIP_UDP_RECVMBOX_SIZE 8
#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1
#define HTTPD_SOCK_ERR_TIMEOUT -2
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_503_SERVICE_UNAVAILABLE 503
#define HTTPD_409_CONFLICT 409
#define HTTPD_202_ACCEPTED 202
#define HTTPD_RESP_USE_STRLEN -1
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTP_EVENT_ON_HEADER 1
#define ESP_EVENT_ANY_ID -1
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef int psa_status_t;
typedef struct { int unused; } portMUX_TYPE;
typedef struct { int unused; } StaticSemaphore_t;
typedef struct { int unused; } psa_hash_operation_t;
typedef struct { int unused; } esp_sntp_config_t;
static inline psa_hash_operation_t psa_hash_operation_init(void) { return (psa_hash_operation_t){0}; }
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(x) ((esp_sntp_config_t){0})
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define cJSON_Number 1
#define cJSON_String 2
#define cJSON_True 3
#define cJSON_False 4
#define cJSON_IsObject(x) ((x) != NULL)
#define cJSON_IsString(x) ((x) && (x)->type == cJSON_String)
#define cJSON_IsNumber(x) ((x) && (x)->type == cJSON_Number)
#define cJSON_IsTrue(x) ((x) && (x)->type == cJSON_True)
#define cJSON_IsFalse(x) ((x) && (x)->type == cJSON_False)
#define cJSON_IsBool(x) ((x) && ((x)->type == cJSON_True || (x)->type == cJSON_False))
#define cJSON_IsArray(x) ((x) && (x)->type == 5)
#define cJSON_ArrayForEach(item,array) for ((item)=(array)->child; (item); (item)=(item)->next)
#define esp_timer_get_time() ((int64_t)0)
typedef struct { uint8_t magic, segment_count, spi_mode, spi_speed, spi_size; uint16_t chip_id; uint8_t reserved[15]; uint8_t hash_appended; } esp_image_header_t;
typedef struct { uint32_t load_addr, data_len; } esp_image_segment_header_t;
typedef struct { uint32_t magic_word, secure_version, reserv1; char version[32]; char project_name[32]; char time[16]; char date[16]; char idf_ver[32]; uint8_t app_elf_sha256[32]; uint8_t reserv2[20]; uint8_t pad[64]; } esp_app_desc_t;
typedef struct { char label[17]; size_t size; int type, subtype; uint32_t address; } esp_partition_t;
typedef uint32_t esp_ota_handle_t;
typedef struct httpd_req { int content_len, method; void *handle; } httpd_req_t;
typedef void *httpd_handle_t;
typedef enum { ESP_OTA_IMG_UNDEFINED, ESP_OTA_IMG_PENDING_VERIFY } esp_ota_img_states_t;
typedef struct { const char *uri; int method; esp_err_t (*handler)(httpd_req_t *); void *user_ctx; } httpd_uri_t;
typedef struct { int server_port, max_open_sockets, max_uri_handlers, recv_wait_timeout, stack_size; bool lru_purge_enable; void (*close_fn)(httpd_handle_t,int); } httpd_config_t;
typedef struct { int event_id; const char *header_key, *header_value; void *user_data; } esp_http_client_event_t;
typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t *);
typedef struct { const char *url; int timeout_ms; bool disable_auto_redirect; esp_err_t (*crt_bundle_attach)(void *); http_event_handle_cb event_handler; void *user_data; } esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
typedef struct cJSON { struct cJSON *child, *next; char *string, *valuestring; double valuedouble; int type; } cJSON;
/* SDK API declarations used by ota.c. */
const char *esp_err_to_name(esp_err_t);
esp_err_t esp_ota_check_image_validity(int,const esp_image_header_t*,const esp_app_desc_t*);
esp_err_t esp_ota_begin(const esp_partition_t*,size_t,esp_ota_handle_t*);
esp_err_t esp_ota_write(esp_ota_handle_t,const void*,size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
const esp_partition_t *esp_partition_find_first(int,int,const char*);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t*);
const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t*);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
const esp_app_desc_t *esp_app_get_description(void);
void esp_fill_random(void*,size_t);
void esp_restart(void);
int httpd_req_recv(httpd_req_t*,char*,size_t);
esp_err_t httpd_start(httpd_handle_t*,const httpd_config_t*);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t*);
esp_err_t httpd_resp_set_status(httpd_req_t*,const char*);
esp_err_t httpd_resp_set_type(httpd_req_t*,const char*);
esp_err_t httpd_resp_set_hdr(httpd_req_t*,const char*,const char*);
esp_err_t httpd_resp_send(httpd_req_t*,const char*,ssize_t);
esp_err_t httpd_resp_send_err(httpd_req_t*,int,const char*);
esp_err_t httpd_req_async_handler_begin(httpd_req_t*,httpd_req_t**);
esp_err_t httpd_req_async_handler_complete(httpd_req_t*);
size_t httpd_req_get_hdr_value_len(httpd_req_t*,const char*);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t*,const char*,char*,size_t);
int httpd_req_to_sockfd(httpd_req_t*);
void *httpd_sess_get_transport_ctx(httpd_handle_t,int);
esp_err_t httpd_sess_set_transport_ctx(httpd_handle_t,int,void*,void(*)(void*));
esp_err_t httpd_sess_trigger_close(httpd_handle_t,int);
void *xQueueCreate(unsigned,size_t);
BaseType_t xQueueSend(void*,const void*,TickType_t);
BaseType_t xQueueReceive(void*,void*,TickType_t);
void vQueueDelete(void*);
void *xSemaphoreCreateBinaryStatic(void*);
BaseType_t xSemaphoreTake(void*,TickType_t);
BaseType_t xSemaphoreGive(void*);
BaseType_t xTaskCreate(void(*)(void*),const char*,uint32_t,void*,unsigned,void**);
void vTaskDelete(void*);
void vTaskDelay(TickType_t);
TickType_t xTaskGetTickCount(void);
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t*);
esp_err_t esp_netif_sntp_sync_wait(TickType_t);
esp_err_t esp_crt_bundle_attach(void*);
void *esp_http_client_init(const esp_http_client_config_t*);
esp_err_t esp_http_client_set_header(void*,const char*,const char*);
esp_err_t esp_http_client_open(void*,int);
int64_t esp_http_client_fetch_headers(void*);
bool esp_http_client_is_chunked_response(void*);
void vSemaphoreDelete(void*);
int esp_http_client_get_status_code(void*);
int esp_http_client_read(void*,char*,int);
bool esp_http_client_is_complete_data_received(void*);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(void*);
const char *esp_http_client_get_header(void*,const char*);
psa_status_t psa_crypto_init(void);
psa_status_t psa_hash_setup(psa_hash_operation_t*,int);
psa_status_t psa_hash_update(psa_hash_operation_t*,const uint8_t*,size_t);
psa_status_t psa_hash_verify(psa_hash_operation_t*,const uint8_t*,size_t);
psa_status_t psa_hash_abort(psa_hash_operation_t*);
void *cJSON_ParseWithLengthOpts(const char*,size_t,const char**,bool);
void cJSON_Delete(cJSON*);
cJSON *cJSON_GetObjectItemCaseSensitive(cJSON*,const char*);
