/*
 * EconetWiFi
 * Copyright (c) 2025 Paul G. Banks <https://paulbanks.org/projects/econet>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the LICENSE file in the project root for full license information.
 */

#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_check.h"
#include "config.h"

config_wifi_t config_wifi;

static const char *TAG = "config";
static const char *ECONET_CONFIG_FILE = "/user/econet_cfg.bin";

static esp_err_t _save_config(const char *config_name, const void *value, size_t length)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open("config", NVS_READWRITE, &h), TAG, "nvs_open");
    ESP_RETURN_ON_ERROR(nvs_set_blob(h, config_name, value, length), TAG, "set_blob");
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t _load_config(const char *config_name, void *value, size_t length)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("config", NVS_READONLY, &h);
    if (err != ESP_OK)
        return err;
    size_t size = length;
    err = nvs_get_blob(h, config_name, value, &size);
    nvs_close(h);
    return err;
}

esp_err_t config_save_wifi(void)
{
    return _save_config("wifi", &config_wifi, sizeof(config_wifi));
}

esp_err_t config_load_wifi(void)
{
    esp_err_t err = _load_config("wifi", &config_wifi, sizeof(config_wifi));
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Using default WiFi configuration");
        memset(&config_wifi, 0, sizeof(config_wifi));
        strcpy((char *)config_wifi.ap.ap.ssid, "nbreak-econet");
        config_wifi.ap_enabled = true;
        config_wifi.ap.ap.authmode = WIFI_AUTH_OPEN;
        config_wifi.ap.ap.max_connection = 3;
    }
    return ESP_OK;
}

esp_err_t config_save_econet(const cJSON *settings)
{
    FILE *fp = fopen("/user/econet_cfg.tmp", "w");
    if (!fp)
    {
        ESP_LOGW(TAG, "Could not open temp file for writing");
        return ESP_OK;
    }

    esp_err_t res = ESP_FAIL;
    char *json_str = cJSON_PrintUnformatted(settings);
    if (json_str != NULL)
    {
        fputs(json_str, fp);
        free(json_str);
        fclose(fp);
        rename("/user/econet_cfg.tmp", ECONET_CONFIG_FILE);
        res = ESP_OK;
    }

    return res;
}

cJSON *config_load_econet_json(void)
{
    FILE *fp = fopen(ECONET_CONFIG_FILE, "r");
    if (!fp)
    {
        ESP_LOGW(TAG, "Could not Econet config file");
        return NULL;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Read file into buffer
    char *buffer = malloc(size + 1);
    if (buffer == NULL)
    {
        fclose(fp);
        ESP_LOGE(TAG, "Could not allocate buffer for file");
        return NULL;
    }
    fread(buffer, 1, size, fp);
    buffer[size] = '\0';
    fclose(fp);

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);

    return root;
}

esp_err_t config_load_econet(config_cb_econet_station eco_cb, config_cb_aun_station aun_cb)
{
    FILE *fp = fopen(ECONET_CONFIG_FILE, "r");
    if (!fp)
    {
        ESP_LOGW(TAG, "Could not Econet config file");
        return ESP_OK;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Read file into buffer
    char *buffer = malloc(size + 1);
    if (buffer == NULL)
    {
        fclose(fp);
        ESP_LOGE(TAG, "Could not allocate buffer for file");
        return ESP_ERR_NO_MEM;
    }
    fread(buffer, 1, size, fp);
    buffer[size] = '\0';
    fclose(fp);

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);

    // Configure Econet stations
    cJSON *cfgs = cJSON_GetObjectItemCaseSensitive(root, "econetStations");
    if (cfgs)
    {
        for (cJSON *item = cfgs->child; item != NULL; item = item->next)
        {

            cJSON *station_id = cJSON_GetObjectItem(item, "station_id");
            cJSON *udp_port = cJSON_GetObjectItem(item, "udp_port");

            bool is_ok = cJSON_IsNumber(station_id) &&
                         cJSON_IsNumber(udp_port);

            if (is_ok && station_id->valueint && udp_port->valueint)
            {
                config_econet_station_t cfg = {
                    .station_id = station_id->valueint,
                    .network_id = 0,
                    .local_udp_port = udp_port->valueint};
                eco_cb(&cfg);
            }
        }
    }

    cfgs = cJSON_GetObjectItemCaseSensitive(root, "aunStations");
    if (cfgs)
    {
        for (cJSON *item = cfgs->child; item != NULL; item = item->next)
        {

            cJSON *station_id = cJSON_GetObjectItem(item, "station_id");
            cJSON *udp_port = cJSON_GetObjectItem(item, "udp_port");
            cJSON *remote_ip = cJSON_GetObjectItem(item, "remote_ip");

            bool is_ok = cJSON_IsNumber(station_id) &&
                         cJSON_IsNumber(udp_port) &&
                         cJSON_IsString(remote_ip);

            if (is_ok && station_id->valueint && udp_port->valueint)
            {
                config_aun_station_t cfg = {
                    .station_id = station_id->valueint,
                    .network_id = 0,
                    .udp_port = udp_port->valueint};
                snprintf(cfg.remote_address, sizeof(cfg.remote_address), "%s", remote_ip->valuestring);
                aun_cb(&cfg);
            }
        }
    }

    cJSON_Delete(root);

    return ESP_OK;
}

void config_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    config_load_wifi();
}
