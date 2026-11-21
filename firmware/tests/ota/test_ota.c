/* This translation unit includes production ota.c: all stream/claim assertions execute its code. */
#include "../../main/app/ota.c"
#include <assert.h>

static const esp_partition_t s_partition = {.label = "ota_1", .size = 4096};
static uint8_t s_image[BYAI_OTA_PREFIX_SIZE + 41];
static size_t s_read_offset;
static size_t s_chunk_limit;
static esp_err_t s_write_error;
static esp_err_t s_end_error;
static unsigned s_begin_count, s_write_count, s_end_count, s_set_boot_count, s_abort_count;
static bool s_complete_message, s_hash_ok;
static TickType_t s_ticks;

static void make_image(const char *version)
{
    memset(s_image, 0x5a, sizeof(s_image));
    esp_image_header_t header = {.magic = ESP_IMAGE_HEADER_MAGIC, .segment_count = 1,
        .chip_id = ESP_CHIP_ID_ESP32S3, .hash_appended = 1};
    esp_image_segment_header_t segment = {.data_len = sizeof(esp_app_desc_t)};
    esp_app_desc_t desc = {.magic_word = ESP_APP_DESC_MAGIC_WORD};
    memcpy(desc.version, version, strlen(version) + 1);
    memcpy(desc.project_name, "BYAI", 5);
    memcpy(s_image, &header, sizeof(header));
    memcpy(s_image + sizeof(header), &segment, sizeof(segment));
    memcpy(s_image + sizeof(header) + sizeof(segment), &desc, sizeof(desc));
}

static void reset_fakes(void)
{
    s_read_offset = 0;
    s_chunk_limit = 7; /* deliberately short reads, including across the 288-byte prefix */
    s_write_error = ESP_OK;
    s_end_error = ESP_OK;
    s_begin_count = s_write_count = s_end_count = s_set_boot_count = s_abort_count = 0;
    s_complete_message = true;
    s_hash_ok = true;
    s_ticks = 1;
}

static int local_recv(httpd_req_t *request, char *buffer, size_t capacity)
{
    (void)request;
    if (s_read_offset == sizeof(s_image)) return 0;
    size_t amount = sizeof(s_image) - s_read_offset;
    if (amount > capacity) amount = capacity;
    if (amount > s_chunk_limit) amount = s_chunk_limit;
    memcpy(buffer, s_image + s_read_offset, amount);
    s_read_offset += amount;
    return (int)amount;
}

static int github_recv(void *client, char *buffer, int capacity)
{
    (void)client;
    if (s_read_offset == sizeof(s_image)) return 0;
    size_t amount = sizeof(s_image) - s_read_offset;
    if (amount > (size_t)capacity) amount = (size_t)capacity;
    if (amount > s_chunk_limit) amount = s_chunk_limit;
    memcpy(buffer, s_image + s_read_offset, amount);
    s_read_offset += amount;
    return (int)amount;
}

/* Production dependencies are substituted at the SDK boundary only. */
int httpd_req_recv(httpd_req_t *req, char *buffer, size_t cap) { return local_recv(req, buffer, cap); }
int esp_http_client_read(void *client, char *buffer, int cap) { return github_recv(client, buffer, cap); }
bool esp_http_client_is_complete_data_received(void *client) { (void)client; return s_complete_message; }
esp_err_t esp_ota_check_image_validity(int type, const esp_image_header_t *h, const esp_app_desc_t *d)
{ (void)type; (void)h; (void)d; return ESP_OK; }
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t size, esp_ota_handle_t *handle)
{ (void)p; (void)size; ++s_begin_count; *handle = 7; return ESP_OK; }
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size)
{ (void)handle; (void)data; (void)size; ++s_write_count; return s_write_error; }
esp_err_t esp_ota_end(esp_ota_handle_t handle)
{ (void)handle; ++s_end_count; return s_end_error; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p)
{ (void)p; ++s_set_boot_count; return ESP_OK; }
esp_err_t esp_ota_abort(esp_ota_handle_t handle)
{ (void)handle; ++s_abort_count; return ESP_OK; }
psa_status_t psa_hash_update(psa_hash_operation_t *op, const uint8_t *data, size_t size)
{ (void)op; (void)data; (void)size; return PSA_SUCCESS; }
psa_status_t psa_hash_verify(psa_hash_operation_t *op, const uint8_t *digest, size_t size)
{ (void)op; (void)digest; (void)size; return s_hash_ok ? PSA_SUCCESS : -1; }
TickType_t xTaskGetTickCount(void) { return s_ticks++; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test-error"; }

static esp_err_t run_local(byai_ota_snapshot_t *snapshot)
{
    httpd_req_t request = {.content_len = sizeof(s_image)};
    ota_stream_t stream = {.partition = &s_partition, .total = sizeof(s_image), .request = &request};
    return process_image(&stream, snapshot);
}

static esp_err_t run_github(byai_ota_snapshot_t *snapshot, const char *version)
{
    ota_stream_t stream = {.partition = &s_partition, .total = sizeof(s_image),
        .client = (void *)1, .github = true, .hash_active = true};
    snprintf(stream.version, sizeof(stream.version), "%s", version);
    return process_image(&stream, snapshot);
}

static void test_short_prefix_and_success(void)
{
    reset_fakes(); make_image("test-1");
    byai_ota_snapshot_t snapshot = {.total_bytes = sizeof(s_image)};
    assert(run_local(&snapshot) == ESP_OK);
    assert(s_read_offset == sizeof(s_image));
    assert(s_begin_count == 1 && s_write_count > 1 && s_end_count == 1 && s_set_boot_count == 1);
}

static void test_version_and_hash_rejections(void)
{
    reset_fakes(); make_image("actual-version");
    byai_ota_snapshot_t snapshot = {.total_bytes = sizeof(s_image)};
    assert(run_github(&snapshot, "different-version") == ESP_ERR_OTA_VALIDATE_FAILED);
    assert(s_begin_count == 0 && s_set_boot_count == 0);

    reset_fakes(); make_image("expected-version"); s_hash_ok = false;
    snapshot = (byai_ota_snapshot_t){.total_bytes = sizeof(s_image)};
    assert(run_github(&snapshot, "expected-version") == ESP_ERR_OTA_VALIDATE_FAILED);
    assert(s_begin_count == 1 && s_end_count == 0 && s_set_boot_count == 0);
}

static void test_write_and_end_failure_lifetimes(void)
{
    reset_fakes(); make_image("local"); s_write_error = ESP_FAIL;
    byai_ota_snapshot_t snapshot = {.total_bytes = sizeof(s_image)};
    httpd_req_t request = {.content_len = sizeof(s_image)};
    ota_stream_t stream = {.partition = &s_partition, .total = sizeof(s_image), .request = &request};
    assert(process_image(&stream, &snapshot) == ESP_FAIL);
    assert(stream.handle_valid && s_end_count == 0 && s_set_boot_count == 0);
    if (stream.handle_valid) assert(esp_ota_abort(stream.handle) == ESP_OK);
    assert(s_abort_count == 1);

    reset_fakes(); make_image("local"); s_end_error = ESP_FAIL;
    snapshot = (byai_ota_snapshot_t){.total_bytes = sizeof(s_image)};
    stream = (ota_stream_t){.partition = &s_partition, .total = sizeof(s_image), .request = &request};
    assert(process_image(&stream, &snapshot) == ESP_FAIL);
    assert(!stream.handle_valid && s_end_count == 1 && s_set_boot_count == 0);
    if (stream.handle_valid) (void)esp_ota_abort(stream.handle);
    assert(s_abort_count == 0);
}

static void test_complete_message_and_length(void)
{
    reset_fakes(); make_image("v"); s_complete_message = false;
    byai_ota_snapshot_t snapshot = {.total_bytes = sizeof(s_image)};
    assert(run_github(&snapshot, "v") == ESP_ERR_INVALID_SIZE);
    assert(s_set_boot_count == 0 && s_end_count == 0);

    reset_fakes(); make_image("v");
    snapshot = (byai_ota_snapshot_t){.total_bytes = sizeof(s_image) - 1};
    ota_stream_t stream = {.partition = &s_partition, .total = sizeof(s_image) - 1,
        .client = (void *)1, .github = true, .hash_active = true};
    snprintf(stream.version, sizeof(stream.version), "%s", "v");
    assert(process_image(&stream, &snapshot) == ESP_ERR_INVALID_SIZE);
    assert(s_set_boot_count == 0 && s_end_count == 0);
}

static void test_claim_mutual_exclusion_and_reclaim(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_initialized = true;
    s_snapshot.boot_confirmed = true;
    s_snapshot.max_image_size = s_partition.size;
    s_update_partition = &s_partition;
    byai_ota_set_network_ready(true);
    s_owner = false;
    assert(byai_ota_upload_claim(sizeof(s_image), OTA_SOURCE_LOCAL, 0) == ESP_OK);
    assert(byai_ota_upload_claim(0, OTA_SOURCE_GITHUB, 45) == ESP_ERR_OTA_ALREADY_IN_PROGRESS);
    byai_ota_upload_fail(ESP_FAIL, "injected source failure");
    byai_ota_upload_release();
    assert(byai_ota_upload_claim(0, OTA_SOURCE_GITHUB, 46) == ESP_OK);
    byai_ota_snapshot_t snapshot;
    assert(byai_ota_snapshot_get(&snapshot));
    assert(snapshot.release_id == 46 && snapshot.state == BYAI_OTA_DOWNLOADING);
    byai_ota_upload_release();
    assert(byai_ota_upload_claim(sizeof(s_image), OTA_SOURCE_LOCAL, 0) == ESP_OK);
    byai_ota_upload_release();
}

int main(void)
{
    test_short_prefix_and_success();
    test_version_and_hash_rejections();
    test_write_and_end_failure_lifetimes();
    test_complete_message_and_length();
    test_claim_mutual_exclusion_and_reclaim();
    puts("ota source-driven tests passed");
    return 0;
}
