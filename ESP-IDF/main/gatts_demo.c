/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/****************************************************************************
*
* This file is for gatt server. It can send adv data, be connected by client.
* Run the gatt_client demo, the client demo will automatically connect to the gatt_server demo.
* Client demo will enable gatt_server's notify after connection. Then two devices will exchange
* data.
*
****************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
/* #include "freertos/heap_regions.h" */
/*  */
#include "esp_system.h"
#include "esp_task.h"
#include "esp_log.h"
/* #include "esp_heap_alloc_caps.h" */
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "sdkconfig.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "smbus.h"
#include "i2c-lcd1602.h"
#include "ws2812.h"

#define WS2812_PIN 13
#define delay_ms(ms) vTaskDelay((ms)/portTICK_RATE_MS)

#define TAG "app"
#define GATTS_TAG "GATTS_DEMO"
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_TX_BUF_LEN 0
#define I2C_MASTER_RX_BUF_LEN 0
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_SCL_IO 22
#define GPIO_INPUT_DIP_1 2
#define GPIO_INPUT_DIP_2 0
#define GPIO_INPUT_DIP_3 4
#define GPIO_INPUT_DIP_4 5
#define GPIO_INPUT_11 34
#define GPIO_INPUT_12 15
#define GPIO_INPUT_21 23
#define GPIO_INPUT_22 16
#define GPIO_INPUT_PIN_SEL ((1ULL<<GPIO_INPUT_DIP_1)|(1ULL<<GPIO_INPUT_DIP_2)|(1ULL<<GPIO_INPUT_DIP_3)|(1ULL<<GPIO_INPUT_DIP_4)|(1ULL<<GPIO_INPUT_11)|(1ULL<<GPIO_INPUT_12)|(1ULL<<GPIO_INPUT_21)|(1ULL<<GPIO_INPUT_22))
#define ESP_INTR_FLAG_DEFAULT 0

static xQueueHandle gpio_evt_queue=NULL;

///Declare the static function
static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

#define GATTS_SERVICE_UUID_TEST_A   0x00FF
#define GATTS_CHAR_UUID_TEST_A      0xFF01
#define GATTS_DESCR_UUID_TEST_A     0x3333
#define GATTS_NUM_HANDLE_TEST_A     4

#define GATTS_SERVICE_UUID_TEST_B   0x000C
#define GATTS_CHAR_UUID_TEST_B      0X0C19
#define GATTS_DESCR_UUID_TEST_B     0x2222
#define GATTS_NUM_HANDLE_TEST_B     4

#define TEST_DEVICE_NAME            "ESP_GATTS_DEMO"
#define TEST_MANUFACTURER_DATA_LEN  17

#define GATTS_DEMO_CHAR_VAL_LEN_MAX 0x40

#define PREPARE_BUF_MAX_SIZE 1024


char* send_str = "Study #";
char get_str[40] = "no data";
char got_str[40] = "";
char report_list[20][40] = {0};
char test_list[20][40] ={0};
char goods_list[20][40] = {0};
char events_list[20][40] = {0};
int rNum=0;
int tNum=0;
int gNum=0;
int eNum=0;
int subject[3][20][2]={0};
int subEventNum[3][4]={0};
int subNum[3]={0,0,0};
char kind='a';
bool twoPacket=false;
int writeCount=0;
int switchNum1=16;
int switchNum2=0;

uint8_t char1_str[] = {0x11,0x22,0x33};
esp_gatt_char_prop_t a_property = 0;
esp_gatt_char_prop_t b_property = 0;

esp_attr_value_t gatts_demo_char1_val =
{
    .attr_max_len = GATTS_DEMO_CHAR_VAL_LEN_MAX,
    .attr_len     = sizeof(char1_str),
    .attr_value   = char1_str,
};

static uint8_t adv_config_done = 0;
#define adv_config_flag      (1 << 0)
#define scan_rsp_config_flag (1 << 1)

#ifdef CONFIG_SET_RAW_ADV_DATA
static uint8_t raw_adv_data[] = {
        0x02, 0x01, 0x06,
        0x02, 0x0a, 0xeb, 0x03, 0x03, 0xab, 0xcd
};
static uint8_t raw_scan_rsp_data[] = {
        0x0f, 0x09, 0x45, 0x53, 0x50, 0x5f, 0x47, 0x41, 0x54, 0x54, 0x53, 0x5f, 0x44,
        0x45, 0x4d, 0x4f
};
#else

static uint8_t adv_service_uuid128[32] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xEE, 0x01, 0x00, 0x00,
    //second uuid, 32bit, [12], [13], [14], [15] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
};

// The length of adv data must be less than 31 bytes
//static uint8_t test_manufacturer[TEST_MANUFACTURER_DATA_LEN] =  {0x12, 0x23, 0x45, 0x56};
//adv data
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x00,
    .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 32,
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};
// scan response data
static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 32,
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

#endif /* CONFIG_SET_RAW_ADV_DATA */

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

#define PROFILE_NUM 2
#define PROFILE_A_APP_ID 0
#define PROFILE_B_APP_ID 1

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};


static void i2c_master_init(void)
{
    int i2c_master_port = I2C_MASTER_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;  // GY-2561 provides 10kΩ pullups
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;  // GY-2561 provides 10kΩ pullups
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    i2c_param_config(i2c_master_port, &conf);
    i2c_driver_install(i2c_master_port, conf.mode,
                       I2C_MASTER_RX_BUF_LEN,
                       I2C_MASTER_TX_BUF_LEN, 0);
}

/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gatts_cb = gatts_profile_a_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
    [PROFILE_B_APP_ID] = {
        .gatts_cb = gatts_profile_b_event_handler,                   /* This demo does not implement, similar as profile A */
        .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

typedef struct {
    uint8_t                 *prepare_buf;
    int                     prepare_len;
} prepare_type_env_t;

static prepare_type_env_t a_prepare_write_env;
static prepare_type_env_t b_prepare_write_env;

void example_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);
void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
#ifdef CONFIG_SET_RAW_ADV_DATA
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        adv_config_done &= (~adv_config_flag);
        if (adv_config_done==0){
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        adv_config_done &= (~scan_rsp_config_flag);
        if (adv_config_done==0){
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
#else
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~adv_config_flag);
        if (adv_config_done == 0){
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~scan_rsp_config_flag);
        if (adv_config_done == 0){
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
#endif
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        //advertising start complete event to indicate advertising start successfully or failed
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTS_TAG, "Advertising start failed\n");
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTS_TAG, "Advertising stop failed\n");
        }
        else {
            ESP_LOGI(GATTS_TAG, "Stop adv successfully\n");
        }
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
         ESP_LOGI(GATTS_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                  param->update_conn_params.status,
                  param->update_conn_params.min_int,
                  param->update_conn_params.max_int,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}

void example_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param){
    esp_gatt_status_t status = ESP_GATT_OK;
    if (param->write.need_rsp){
        if (param->write.is_prep){
            if (prepare_write_env->prepare_buf == NULL) {
                prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE*sizeof(uint8_t));
                prepare_write_env->prepare_len = 0;
                if (prepare_write_env->prepare_buf == NULL) {
                    ESP_LOGE(GATTS_TAG, "Gatt_server prep no mem\n");
                    status = ESP_GATT_NO_RESOURCES;
                }
            } else {
                if(param->write.offset > PREPARE_BUF_MAX_SIZE) {
                    status = ESP_GATT_INVALID_OFFSET;
                } else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE) {
                    status = ESP_GATT_INVALID_ATTR_LEN;
                }
            }

            esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
            gatt_rsp->attr_value.len = param->write.len;
            gatt_rsp->attr_value.handle = param->write.handle;
            gatt_rsp->attr_value.offset = param->write.offset;
            gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
            memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
            esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
            if (response_err != ESP_OK){
               ESP_LOGE(GATTS_TAG, "Send response error\n");
            }
            free(gatt_rsp);
            if (status != ESP_GATT_OK){
                return;
            }
            memcpy(prepare_write_env->prepare_buf + param->write.offset,
                   param->write.value,
                   param->write.len);
            prepare_write_env->prepare_len += param->write.len;

        }else{
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, NULL);
        }
    }
}

void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param){
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC){
        esp_log_buffer_hex(GATTS_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
    }else{
        ESP_LOGI(GATTS_TAG,"ESP_GATT_PREP_WRITE_CANCEL");
    }
    if (prepare_write_env->prepare_buf) {
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    prepare_write_env->prepare_len = 0;
}

static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {

    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(GATTS_TAG, "REGISTER_APP_EVT, status %d, app_id %d\n", param->reg.status, param->reg.app_id);
        gl_profile_tab[PROFILE_A_APP_ID].service_id.is_primary = true;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_TEST_A;

        esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(TEST_DEVICE_NAME);
        if (set_dev_name_ret){
            ESP_LOGE(GATTS_TAG, "set device name failed, error code = %x", set_dev_name_ret);
        }
#ifdef CONFIG_SET_RAW_ADV_DATA
        esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
        if (raw_adv_ret){
            ESP_LOGE(GATTS_TAG, "config raw adv data failed, error code = %x ", raw_adv_ret);
        }
        adv_config_done |= adv_config_flag;
        esp_err_t raw_scan_ret = esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data, sizeof(raw_scan_rsp_data));
        if (raw_scan_ret){
            ESP_LOGE(GATTS_TAG, "config raw scan rsp data failed, error code = %x", raw_scan_ret);
        }
        adv_config_done |= scan_rsp_config_flag;
#else
        //config adv data
        esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
        if (ret){
            ESP_LOGE(GATTS_TAG, "config adv data failed, error code = %x", ret);
        }
        adv_config_done |= adv_config_flag;
        //config scan response data
        ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
        if (ret){
            ESP_LOGE(GATTS_TAG, "config scan response data failed, error code = %x", ret);
        }
        adv_config_done |= scan_rsp_config_flag;

#endif
        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_A_APP_ID].service_id, GATTS_NUM_HANDLE_TEST_A);
        break;

    case ESP_GATTS_READ_EVT: {
        ESP_LOGI(GATTS_TAG, "GATT_READ_EVT, conn_id %d, trans_id %d, handle %d\n", param->read.conn_id, param->read.trans_id, param->read.handle);
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = 4;
        rsp.attr_value.value[0] = 0xde;
        rsp.attr_value.value[1] = 0xed;
        rsp.attr_value.value[2] = 0xbe;
        rsp.attr_value.value[3] = 0xef;
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,ESP_GATT_OK, &rsp);

        break;
    }
    case ESP_GATTS_WRITE_EVT: {
        ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %d, handle %d", param->write.conn_id, param->write.trans_id, param->write.handle);
        if (!param->write.is_prep){
            ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, value len %d, value :", param->write.len);
            esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);
            if (gl_profile_tab[PROFILE_A_APP_ID].descr_handle == param->write.handle && param->write.len == 2){
                uint16_t descr_value = param->write.value[1]<<8 | param->write.value[0];
                if (descr_value == 0x0001){
                    if (a_property & ESP_GATT_CHAR_PROP_BIT_NOTIFY){
                        ESP_LOGI(GATTS_TAG, "notify enable");
                        uint8_t notify_data[15];
                        for (int i = 0; i < sizeof(notify_data); ++i)
                        {
                            notify_data[i] = i%0xff;
                        }
                        //the size of notify_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                                sizeof(notify_data), notify_data, false);
                    }
                }else if (descr_value == 0x0002){
                    if (a_property & ESP_GATT_CHAR_PROP_BIT_INDICATE){
                        ESP_LOGI(GATTS_TAG, "indicate enable");
                        uint8_t indicate_data[15];
                        for (int i = 0; i < sizeof(indicate_data); ++i)
                        {
                            indicate_data[i] = i%0xff;
                        }
                        //the size of indicate_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                                sizeof(indicate_data), indicate_data, true);
                    }
                }
                else if (descr_value == 0x0000){
                    ESP_LOGI(GATTS_TAG, "notify/indicate disable ");
                }else{
                    ESP_LOGE(GATTS_TAG, "unknown descr value");
                    esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);
                }

            }
        }
        example_write_event_env(gatts_if, &a_prepare_write_env, param);
        break;
    }
    case ESP_GATTS_EXEC_WRITE_EVT:
        ESP_LOGI(GATTS_TAG,"ESP_GATTS_EXEC_WRITE_EVT");
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        example_exec_write_event_env(&a_prepare_write_env, param);
        break;
    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
        break;
    case ESP_GATTS_UNREG_EVT:
        break;
    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(GATTS_TAG, "CREATE_SERVICE_EVT, status %d,  service_handle %d\n", param->create.status, param->create.service_handle);
        gl_profile_tab[PROFILE_A_APP_ID].service_handle = param->create.service_handle;
        gl_profile_tab[PROFILE_A_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_TEST_A;

        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_A_APP_ID].service_handle);
        a_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
        esp_err_t add_char_ret = esp_ble_gatts_add_char(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &gl_profile_tab[PROFILE_A_APP_ID].char_uuid,
                                                        ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                        a_property,
                                                        &gatts_demo_char1_val, NULL);
        if (add_char_ret){
            ESP_LOGE(GATTS_TAG, "add char failed, error code =%x",add_char_ret);
        }
        break;
    case ESP_GATTS_ADD_INCL_SRVC_EVT:
        break;
    case ESP_GATTS_ADD_CHAR_EVT: {
        uint16_t length = 0;
        const uint8_t *prf_char;

        ESP_LOGI(GATTS_TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d\n",
                param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);
        gl_profile_tab[PROFILE_A_APP_ID].char_handle = param->add_char.attr_handle;
        gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_err_t get_attr_ret = esp_ble_gatts_get_attr_value(param->add_char.attr_handle,  &length, &prf_char);
        if (get_attr_ret == ESP_FAIL){
            ESP_LOGE(GATTS_TAG, "ILLEGAL HANDLE");
        }

        ESP_LOGI(GATTS_TAG, "the gatts demo char length = %x\n", length);
        for(int i = 0; i < length; i++){
            ESP_LOGI(GATTS_TAG, "prf_char[%x] =%x\n",i,prf_char[i]);
        }
        esp_err_t add_descr_ret = esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &gl_profile_tab[PROFILE_A_APP_ID].descr_uuid,
                                                                ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        if (add_descr_ret){
            ESP_LOGE(GATTS_TAG, "add char descr failed, error code =%x", add_descr_ret);
        }
        break;
    }
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].descr_handle = param->add_char_descr.attr_handle;
        ESP_LOGI(GATTS_TAG, "ADD_DESCR_EVT, status %d, attr_handle %d, service_handle %d\n",
                 param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
        break;
    case ESP_GATTS_DELETE_EVT:
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGI(GATTS_TAG, "SERVICE_START_EVT, status %d, service_handle %d\n",
                 param->start.status, param->start.service_handle);
        break;
    case ESP_GATTS_STOP_EVT:
        break;
    case ESP_GATTS_CONNECT_EVT: {
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        /* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
        conn_params.latency = 0;
        conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
        conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
        conn_params.timeout = 400;    // timeout = 400*10ms = 4000ms
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
                 param->connect.conn_id,
                 param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                 param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = param->connect.conn_id;
        //start sent the update connection parameters to the peer device.
        esp_ble_gap_update_conn_params(&conn_params);
        break;
    }
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GATTS_CONF_EVT:
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_CONF_EVT, status %d", param->conf.status);
        if (param->conf.status != ESP_GATT_OK){
            esp_log_buffer_hex(GATTS_TAG, param->conf.value, param->conf.len);
        }
        break;
    case ESP_GATTS_OPEN_EVT:
    case ESP_GATTS_CANCEL_OPEN_EVT:
    case ESP_GATTS_CLOSE_EVT:
    case ESP_GATTS_LISTEN_EVT:
    case ESP_GATTS_CONGEST_EVT:
    default:
        break;
    }
}

/*hensyuu suru tokoro*/
static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(GATTS_TAG, "REGISTER_APP_EVT, status %d, app_id %d\n", param->reg.status, param->reg.app_id);
        gl_profile_tab[PROFILE_B_APP_ID].service_id.is_primary = true;
        gl_profile_tab[PROFILE_B_APP_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[PROFILE_B_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_B_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_TEST_B;

        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_B_APP_ID].service_id, GATTS_NUM_HANDLE_TEST_B);
        break;
    case ESP_GATTS_READ_EVT: {
        ESP_LOGI(GATTS_TAG, "GATT_READ_EVT, conn_id %d, trans_id %d, handle %d\n", param->read.conn_id, param->read.trans_id, param->read.handle);
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = 5;
        /* rsp.attr_value.value[0] = 0xde; */
        /* rsp.attr_value.value[1] = 0xed; */
        /* rsp.attr_value.value[2] = 0xbe; */
        /* rsp.attr_value.value[3] = 0xef; */
        /*sousin naiyou*/
        printf("send chars  ");
        printf("\"%s\"\n",send_str);
        for(int i=0;i<5;i++)rsp.attr_value.value[i] = send_str[i];
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_OK, &rsp);
        send_str="already get"; //change send datas

        break;
    }
    case ESP_GATTS_WRITE_EVT: {
        ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %d, handle %d\n", param->write.conn_id, param->write.trans_id, param->write.handle);
        if (!param->write.is_prep){
            int chara=0;
            int sub=0;
            bool eventdata=true;

            ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, value len %d, value :", param->write.len);
            /* printf("%d",param->write.len); */
            esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);


            sub=param->write.value[param->write.len-1];
            if(twoPacket){
                /* strncat(got_str,get_str,20); */
                /* strncpy(get_str,got_str,40); */
                twoPacket=false;
                writeCount-=1;
            }else {
                writeCount=0;
                memset(get_str,0,sizeof(get_str));
                /* printf("get chars\n"); */
            }
            for(int i=0;i<param->write.len;i++){
                if(param->write.len==1){
                    kind=(char)param->write.value[0];
                    eventdata=false;
                    continue;
                }
                if(param->write.len==20 && (char)param->write.value[19]=='%')twoPacket=true;
                /* printf("%c",(char)(param->write.value[i])); */
                if(param->write.value[i]==127){
                    chara+=127;
                }else{
                    chara+=param->write.value[i];
                    get_str[writeCount]=(char)chara; //change send datas
                    chara=0;
                    writeCount++;
                }
            }

            /* printf("%c\n",kind); */
            if(!twoPacket && eventdata){
                int subkind=0;
                int num=0;
                //strncpy(got_str,get_str,40);
                switch(kind){
                    case 'r':
                        strcpy(report_list[rNum],get_str);
                        rNum++;subkind=0;num=rNum-1;
                        break;
                    case 't':
                        strcpy(test_list[tNum],get_str);
                        tNum++;subkind=1;num=tNum-1;
                        break;
                    case 'g':
                        strcpy(goods_list[gNum],get_str);
                        gNum++;subkind=2;num=gNum-1;
                        break;
                    case 'e':
                        strcpy(events_list[eNum],get_str);
                        eNum++;subkind=3;num=eNum-1;
                        break;
                    default:
                        break;
                }
                if(sub==49 || sub==50 || sub==51){
                    printf("sub: %d\n",sub-49);
                    subject[sub-49][subNum[sub-49]][0]=subkind;
                    subject[sub-49][subNum[sub-49]][1]=num;
                    printf("%d  %d  %s\n",subkind, num, get_str);
                    subEventNum[sub-49][subkind]++;
                    subNum[sub-49]++;
                }
            }


            /*kokode yomeru!!*/

            if (gl_profile_tab[PROFILE_B_APP_ID].descr_handle == param->write.handle && param->write.len == 2){
                uint16_t descr_value= param->write.value[1]<<8 | param->write.value[0];
                if (descr_value == 0x0001){
                    if (b_property & ESP_GATT_CHAR_PROP_BIT_NOTIFY){
                        ESP_LOGI(GATTS_TAG, "notify enable");
                        uint8_t notify_data[15];
                        for (int i = 0; i < sizeof(notify_data); ++i)
                        {
                            notify_data[i] = i%0xff;
                        }
                        //the size of notify_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_B_APP_ID].char_handle,
                                                sizeof(notify_data), notify_data, false);
                    }
                }else if (descr_value == 0x0002){
                    if (b_property & ESP_GATT_CHAR_PROP_BIT_INDICATE){
                        ESP_LOGI(GATTS_TAG, "indicate enable");
                        uint8_t indicate_data[15];
                        for (int i = 0; i < sizeof(indicate_data); ++i)
                        {
                            indicate_data[i] = i%0xff;
                        }
                        //the size of indicate_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_B_APP_ID].char_handle,
                                                sizeof(indicate_data), indicate_data, true);
                    }
                }
                else if (descr_value == 0x0000){
                    ESP_LOGI(GATTS_TAG, "notify/indicate disable ");
                }else{
                    ESP_LOGE(GATTS_TAG, "unknown value");
                }

            }
        }
        example_write_event_env(gatts_if, &b_prepare_write_env, param);
        break;
    }
    case ESP_GATTS_EXEC_WRITE_EVT:
        printf("write 622");
        ESP_LOGI(GATTS_TAG,"ESP_GATTS_EXEC_WRITE_EVT");
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        example_exec_write_event_env(&b_prepare_write_env, param);
        break;
    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
        break;
    case ESP_GATTS_UNREG_EVT:
        break;
    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(GATTS_TAG, "CREATE_SERVICE_EVT, status %d,  service_handle %d\n", param->create.status, param->create.service_handle);
        gl_profile_tab[PROFILE_B_APP_ID].service_handle = param->create.service_handle;
        gl_profile_tab[PROFILE_B_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_B_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_TEST_B;

        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_B_APP_ID].service_handle);
        b_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
        esp_err_t add_char_ret =esp_ble_gatts_add_char( gl_profile_tab[PROFILE_B_APP_ID].service_handle, &gl_profile_tab[PROFILE_B_APP_ID].char_uuid,
                                                        ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                        b_property,
                                                        NULL, NULL);
        if (add_char_ret){
            ESP_LOGE(GATTS_TAG, "add char failed, error code =%x",add_char_ret);
        }
        break;
    case ESP_GATTS_ADD_INCL_SRVC_EVT:
        break;
    case ESP_GATTS_ADD_CHAR_EVT:
        ESP_LOGI(GATTS_TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d\n",
                 param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);

        gl_profile_tab[PROFILE_B_APP_ID].char_handle = param->add_char.attr_handle;
        gl_profile_tab[PROFILE_B_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_B_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_B_APP_ID].service_handle, &gl_profile_tab[PROFILE_B_APP_ID].descr_uuid,
                                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                     NULL, NULL);
        break;
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        gl_profile_tab[PROFILE_B_APP_ID].descr_handle = param->add_char_descr.attr_handle;
        ESP_LOGI(GATTS_TAG, "ADD_DESCR_EVT, status %d, attr_handle %d, service_handle %d\n",
                 param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
        break;
    case ESP_GATTS_DELETE_EVT:
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGI(GATTS_TAG, "SERVICE_START_EVT, status %d, service_handle %d\n",
                 param->start.status, param->start.service_handle);
        break;
    case ESP_GATTS_STOP_EVT:
        break;
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(GATTS_TAG, "CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
                 param->connect.conn_id,
                 param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                 param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
        gl_profile_tab[PROFILE_B_APP_ID].conn_id = param->connect.conn_id;
        break;
    case ESP_GATTS_CONF_EVT:
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_CONF_EVT status %d", param->conf.status);
        if (param->conf.status != ESP_GATT_OK){
            esp_log_buffer_hex(GATTS_TAG, param->conf.value, param->conf.len);
        }
    break;
    case ESP_GATTS_DISCONNECT_EVT:
    case ESP_GATTS_OPEN_EVT:
    case ESP_GATTS_CANCEL_OPEN_EVT:
    case ESP_GATTS_CLOSE_EVT:
    case ESP_GATTS_LISTEN_EVT:
    case ESP_GATTS_CONGEST_EVT:
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        } else {
            ESP_LOGI(GATTS_TAG, "Reg app failed, app_id %04x, status %d\n",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }

    /* If the gatts_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gatts_if == gl_profile_tab[idx].gatts_if) {
                if (gl_profile_tab[idx].gatts_cb) {
                    gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

void lcd1602_task(void * pvParameter)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0,ADC_ATTEN_DB_0);
    /* Set up I2C */
    i2c_master_init();
    i2c_port_t i2c_num = I2C_MASTER_NUM;
    uint8_t address = 0x27;

    // Set up the SMBus
    smbus_info_t * smbus_info = smbus_malloc();
    smbus_init(smbus_info, i2c_num, address);
    smbus_set_timeout(smbus_info, 1000 / portTICK_RATE_MS);

    // Set up the LCD1602 device with backlight off
    i2c_lcd1602_info_t * lcd_info = i2c_lcd1602_malloc();
    i2c_lcd1602_init(lcd_info, smbus_info, true);

    // turn on backlight
    i2c_lcd1602_set_backlight(lcd_info, true);

    i2c_lcd1602_set_cursor(lcd_info, true);
    /* i2c_lcd1602_move_cursor(lcd_info, 0, 0); */
    i2c_lcd1602_set_display(lcd_info, true);

    /* ESP_LOGI(TAG, "clear display and disable cursor"); */
    i2c_lcd1602_clear(lcd_info);
    i2c_lcd1602_set_cursor(lcd_info, false);
    /* int val=0; */
    int count=0;
    int sum=0;
    while(1){
        i2c_lcd1602_clear(lcd_info);
        /* val=adc1_get_raw(ADC1_CHANNEL_5); */
        sum=rNum+tNum+gNum+eNum;
        if(count>sum)count=0;
        if(sum==0){
        }else if(switchNum1==16){
            if(sum>=1){
                i2c_lcd1602_home(lcd_info);
                if(count<rNum){
                    i2c_lcd1602_write_char(lcd_info, 'R');
                    i2c_lcd1602_move_cursor(lcd_info,1,0);
                    i2c_lcd1602_write_string(lcd_info,report_list[count]);
                }else if(count<rNum+tNum){
                    i2c_lcd1602_write_char(lcd_info, 'T');
                    i2c_lcd1602_move_cursor(lcd_info,1,0);
                    i2c_lcd1602_write_string(lcd_info,test_list[count-rNum]);
                }else if(count<rNum+tNum+gNum){
                    i2c_lcd1602_write_char(lcd_info, 'G');
                    i2c_lcd1602_move_cursor(lcd_info,1,0);
                    i2c_lcd1602_write_string(lcd_info,goods_list[count-rNum-tNum]);
                }else if(count<rNum+tNum+gNum+eNum){
                    i2c_lcd1602_write_char(lcd_info, 'E');
                    i2c_lcd1602_move_cursor(lcd_info,1,0);
                    i2c_lcd1602_write_string(lcd_info,events_list[count-rNum-tNum-gNum]);
                }
            }
            count++;
            if(sum>=2){
                i2c_lcd1602_move_cursor(lcd_info,0,1);
                if((count)<rNum){
                    i2c_lcd1602_write_char(lcd_info, 'R');
                    i2c_lcd1602_move_cursor(lcd_info,1,1);
                    i2c_lcd1602_write_string(lcd_info,report_list[count]);
                }else if((count)<rNum+tNum){
                    i2c_lcd1602_write_char(lcd_info, 'T');
                    i2c_lcd1602_move_cursor(lcd_info,1,1);
                    i2c_lcd1602_write_string(lcd_info,test_list[count-rNum]);
                }else if((count)<rNum+tNum+gNum){
                    i2c_lcd1602_write_char(lcd_info, 'G');
                    i2c_lcd1602_move_cursor(lcd_info,1,1);
                    i2c_lcd1602_write_string(lcd_info,goods_list[count-rNum-tNum]);
                }else if((count)<rNum+tNum+gNum+eNum){
                    i2c_lcd1602_write_char(lcd_info, 'E');
                    i2c_lcd1602_move_cursor(lcd_info,1,1);
                    i2c_lcd1602_write_string(lcd_info,events_list[count-rNum-tNum-gNum]);
                }
            }
            count++;
        }else{
            int sub=0;
            if(switchNum1==34){sub=0;
            }else if(switchNum1==15){
                sub=1;
            }else if(switchNum1==23){sub=2;}

            i2c_lcd1602_home(lcd_info);
            if(subject[sub][count][0]==NULL)count=0;
            /* printf("sub:%d   [0]:%d  [1]:%d\n",sub,subject[sub][count][0],subject[sub][count][1]); */
            switch(subject[sub][count][0]){
                case 0:
                    i2c_lcd1602_write_char(lcd_info, 'R');
                    i2c_lcd1602_move_cursor(lcd_info,1,0);
                    i2c_lcd1602_write_string(lcd_info,report_list[subject[sub][count][1]]);
                    break;
                case 1:
                    i2c_lcd1602_write_char(lcd_info, 'T');
                    i2c_lcd1602_move_cursor(lcd_info,1,0);
                    i2c_lcd1602_write_string(lcd_info,test_list[subject[sub][count][1]]);
                    break;
                case 2:
                    i2c_lcd1602_write_char(lcd_info, 'G');
                    i2c_lcd1602_move_cursor(lcd_info,1,0);
                    i2c_lcd1602_write_string(lcd_info,goods_list[subject[sub][count][1]]);
                    break;
                case 3:
                    i2c_lcd1602_write_char(lcd_info, 'E');
                    i2c_lcd1602_move_cursor(lcd_info,1,0);
                    i2c_lcd1602_write_string(lcd_info,events_list[subject[sub][count][1]]);
                    break;
            }
            i2c_lcd1602_move_cursor(lcd_info,0,1);
            count++;
            if(subject[sub][count][0]==NULL)count=0;
            /* printf("sub:%d   [0]:%d  [1]:%d\n",sub,subject[sub][count][0],subject[sub][count][1]); */
            switch(subject[sub][count][0]){
                case 0:
                    i2c_lcd1602_write_char(lcd_info, 'R');
                    i2c_lcd1602_move_cursor(lcd_info,1,1);
                    i2c_lcd1602_write_string(lcd_info,report_list[subject[sub][count][1]]);
                    break;
                case 1:
                    i2c_lcd1602_write_char(lcd_info, 'T');
                    i2c_lcd1602_move_cursor(lcd_info,1,1);
                    i2c_lcd1602_write_string(lcd_info,test_list[subject[sub][count][1]]);
                    break;
                case 2:
                    i2c_lcd1602_write_char(lcd_info, 'G');
                    i2c_lcd1602_move_cursor(lcd_info,1,1);
                    i2c_lcd1602_write_string(lcd_info,goods_list[subject[sub][count][1]]);
                    break;
                case 3:
                    i2c_lcd1602_write_char(lcd_info, 'E');
                    i2c_lcd1602_move_cursor(lcd_info,1,1);
                    i2c_lcd1602_write_string(lcd_info,events_list[subject[sub][count][1]]);
                    break;
            }
            count++;
        }
        vTaskDelay(5000/ portTICK_RATE_MS);
    }

    vTaskDelete(NULL);
}

static void IRAM_ATTR gpio_isr_handler(void* arg){
    uint32_t gpio_num=(uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num,NULL);
}

static void gpio_task(void* arg){
    uint32_t io_num;
    printf("gpio_task\n");
    while(1){
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)){
            if(gpio_get_level(io_num))
            switch(io_num){
                case 2:
                    switchNum2=1;
                    break;
                case 0:
                    switchNum2=2;
                    break;
                case 4:
                    switchNum2=3;
                    break;
                case 5:
                    switchNum2=4;
                    break;
                default:
                    switchNum1=io_num;
                    break;
            }
        }
    }
}

void led_task(void *pvParameters){
  const uint8_t dim_div = 8;
  const uint8_t anim_step = 8;
  const uint8_t anim_max = 256 - anim_step;
  const uint16_t pixel_count = 12; // Number of your "pixels"
  const uint8_t delay = 255; // duration between color changes
  rgbVal color = makeRGBVal(anim_max, 0, 0);
  uint8_t step = 0;
  rgbVal color2 = makeRGBVal(anim_max, 0, 0);
  uint8_t step2 = 0;
  rgbVal *pixels;


  pixels = malloc(sizeof(rgbVal) * pixel_count);
  printf("led start  %d\n",sizeof(pixels));

  while (1) {
    /* printf("%d. %d. %d.\r",pixel_count, color.r, step); */
    color = color2;
    step = step2;

    for (uint16_t i = 0; i < pixel_count; i++) {
        if(switchNum1==16 || switchNum1==15 || switchNum1==23 || switchNum1==34){
            /* if(subEventNum[i/4][i%4] > 0){ */
                int r=0,g=0,b=0;
                if(subEventNum[i/4][i%4]==0){r=0;g=0;b=0;}
                else if(subEventNum[i/4][i%4]==1){r=0;g=0;b=32;}
                else if(subEventNum[i/4][i%4]==2){r=0;g=32;b=0;}
                else if(subEventNum[i/4][i%4]==3){r=24;g=16;b=0;}
                else if(subEventNum[i/4][i%4]>=4){r=64;g=0;b=0;}
                /* if(i%4==0) pixels[i] = makeRGBVal(64,0,0); */
                /* else if(i%4==1) pixels[i] = makeRGBVal(0,64,0); */
                /* else if(i%4==2) pixels[i] = makeRGBVal(0,0,64); */
                /* else if(i%4==3) pixels[i] = makeRGBVal(32,32,32); */
                pixels[i] = makeRGBVal(r,g,b);
            /* }else{ */
            /*     pixels[i] = makeRGBVal(0,0,0); */
            /* } */
        }else if(switchNum1==0){
            pixels[i] = makeRGBVal(color.r/dim_div, color.g/dim_div, color.b/dim_div);
            switch (step) {
                case 0:
                    color.g += anim_step;
                    if (color.g >= anim_max)
                        step++;
                    break;
                case 1:
                    color.r -= anim_step;
                    if (color.r == 0)
                        step++;
                    break;
                case 2:
                    color.b += anim_step;
                    if (color.b >= anim_max)
                        step++;
                    break;
                case 3:
                    color.g -= anim_step;
                    if (color.g == 0)
                        step++;
                    break;
                case 4:
                    color.r += anim_step;
                    if (color.r >= anim_max)
                        step++;
                    break;
                case 5:
                    color.b -= anim_step;
                    if (color.b == 0)
                        step = 0;
                    break;
            }
        }
        if (i == 1) {
            color2 = color;
            step2 = step;
        }
    }


    ws2812_setColors(pixel_count, pixels);

    delay_ms(delay);
    delay_ms(delay);
    delay_ms(delay);
    delay_ms(delay);
  }
}
/* extern void main_led_task(void* args){ */
/*     struct led_strip_t led_strip={ */
/*         .rgb_led_type = RGB_LED_TYPE_WS2812, */
/*         .rmt_channel = RMT_CHANNEL_1, */
/*         .rmt_interrupt_num = LED_STRIP_RMT_INTR_NUM, */
/*         .gpio = GPIO_NUM_13, */
/*         .led_strip_buf_1 = led_strip_buf_1, */
/*         .led_strip_buf_2 = led_strip_buf_2, */
/*         .led_strip_length = LED_STRIP_LENGTH */
/*     }; */
/*     led_strip.access_semaphore = xSemaphoreCreateBinary(); */
/*  */
/*     bool led_init_ok =led_strip_init(&led_strip); */
/*     assert(led_init_ok); */
/*  */
/*     struct led_color_t led_color={ */
/*         .red=10, */
/*         .green=0, */
/*         .blue=0, */
/*     }; */
/*  */
/*     while(1){ */
/*         led_strip_clear(&led_strip); */
/*         for(uint32_t index=0; index < LED_STRIP_LENGTH; index++){ */
/*             led_strip_set_pixel_color(&led_strip,index,&led_color); */
/*         } */
/*         led_strip_show(&led_strip); */
/*  */
/*         led_color.red+=10; */
/*         led_color.red%=255; */
/*         vTaskDelay(1000/portTICK_RATE_MS); */
/*     } */
/* } */

/* extern "C" {void app_main()} */
void app_main(){
    esp_err_t ret;

    // Initialize NVS.
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg); if (ret) {
        ESP_LOGE(GATTS_TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(GATTS_TAG, "%s init bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTS_TAG, "%s enable bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret){
        ESP_LOGE(GATTS_TAG, "gatts register error, error code = %x", ret);
        return;
    }
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret){
        ESP_LOGE(GATTS_TAG, "gap register error, error code = %x", ret);
        return;
    }
    ret = esp_ble_gatts_app_register(PROFILE_A_APP_ID);
    if (ret){
        ESP_LOGE(GATTS_TAG, "gatts app register error, error code = %x", ret);
        return;
    }
    ret = esp_ble_gatts_app_register(PROFILE_B_APP_ID);
    if (ret){
        ESP_LOGE(GATTS_TAG, "gatts app register error, error code = %x", ret);
        return;
    }
    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret){
        ESP_LOGE(GATTS_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }

    gpio_config_t io_conf;
    io_conf.intr_type=GPIO_PIN_INTR_POSEDGE;
    io_conf.pin_bit_mask=GPIO_INPUT_PIN_SEL;
    io_conf.mode=GPIO_MODE_INPUT;
    io_conf.pull_down_en=1;
    gpio_config(&io_conf);
    gpio_evt_queue=xQueueCreate(10,sizeof(uint32_t));
    xTaskCreate(gpio_task, "gpio_task", 2048, NULL, 10, NULL);
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(GPIO_INPUT_DIP_1, gpio_isr_handler, (void*) GPIO_INPUT_DIP_1);
    gpio_isr_handler_add(GPIO_INPUT_DIP_2, gpio_isr_handler, (void*) GPIO_INPUT_DIP_2);
    gpio_isr_handler_add(GPIO_INPUT_DIP_3, gpio_isr_handler, (void*) GPIO_INPUT_DIP_3);
    gpio_isr_handler_add(GPIO_INPUT_DIP_4, gpio_isr_handler, (void*) GPIO_INPUT_DIP_4);
    gpio_isr_handler_add(GPIO_INPUT_11, gpio_isr_handler, (void*) GPIO_INPUT_11);
    gpio_isr_handler_add(GPIO_INPUT_12, gpio_isr_handler, (void*) GPIO_INPUT_12);
    gpio_isr_handler_add(GPIO_INPUT_21, gpio_isr_handler, (void*) GPIO_INPUT_21);
    gpio_isr_handler_add(GPIO_INPUT_22, gpio_isr_handler, (void*) GPIO_INPUT_22);

    xTaskCreate(&lcd1602_task, "lcd1602_task", 4096, NULL, 5, NULL);

    /* TaskHandle_t main_task_handle; */
    /* BaseType_t task_created = xTaskCreate(main_led_task,"main_led_task",ESP_TASK_MAIN_STACK,NULL,ESP_TASK_MAIN_PRIO,&main_task_handle); */
    /* (void)task_created; */
    /* vTaskDelete(NULL); */
    /* xTaskCreate(ws2812_task, "led_task", 12288, NULL,5,NULL); */

    ws2812_init(WS2812_PIN);
    xTaskCreate(led_task , "led_task", 4096, NULL,10,NULL);

    return;
}
