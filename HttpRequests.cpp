//
// Created by Melanie on 26/01/2025.
//

#include <esp_err.h>
#include <esp_http_client.h>
#include <functional>
#include "HttpRequests.h"

mutex HttpRequests::m_httpLock;

esp_err_t HttpRequests::postUrl(const std::string& url, std::vector<uint8_t>& data, const function<void(vector<uint8_t>&)>& func)
{
    esp_err_t err;

    m_httpLock.lock();

    struct DownloadInfo info;
    std::vector<uint8_t> body = data;

    data.push_back(0);
    printf("Posting %s to %s\r\n", data.data(), url.c_str());

    esp_http_client_config_t conf;
    memset(&conf, 0, sizeof(conf));
    conf = {
        .url = url.c_str(),
        .auth_type = HTTP_AUTH_TYPE_NONE,
        .user_agent = "ESP32 Mozilla Compatible",
        .method = HTTP_METHOD_POST,
        .event_handler = HttpRequests::DownloadHandler,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .buffer_size = 8192,
        .user_data = &info,
        .keep_alive_enable = true
    };

    while (true)
    {
        esp_http_client *handle = esp_http_client_init(&conf);
        esp_http_client_set_post_field(handle, (const char *) body.data(), (int) body.size());
        esp_http_client_set_header(handle, "Content-type", "text/plain");
        esp_http_client_set_header(handle, "Accept", "application/json");

        err = esp_http_client_perform(handle);

        esp_http_client_close(handle);

        if (err == ESP_OK)
        {
            m_httpLock.unlock();
            if (func)
                (func)(info.data);
            return ESP_OK;
        }
        else
        {
            m_httpLock.unlock();

            printf("Error %d fetching %s\r\n", err, url.c_str());
            return err;
        }
    }
}

esp_err_t HttpRequests::getUrl(const std::string& url, const function<void(vector<uint8_t>&)>& func)
{
    esp_err_t err;

    printf("Requesting %s\r\n", url.c_str());

    m_httpLock.lock();

    struct DownloadInfo info;

    esp_http_client_config_t conf;
    memset(&conf, 0, sizeof(conf));
    conf = {
        .url = url.c_str(),
        .auth_type = HTTP_AUTH_TYPE_NONE,
        .user_agent = "ESP32 Mozilla Compatible",
        .method = HTTP_METHOD_GET,
        .event_handler = HttpRequests::DownloadHandler,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .buffer_size = 8192,
        .user_data = &info,
        .keep_alive_enable = true
    };

    while (true)
    {
        esp_http_client *handle = esp_http_client_init(&conf);

        err = esp_http_client_perform(handle);

        esp_http_client_close(handle);

        printf("Perform done\r\n");

        if (err == ESP_OK)
        {
            m_httpLock.unlock();
            if (func)
                (func)(info.data);
            return ESP_OK;
        }
        else
        {
            m_httpLock.unlock();

            printf("Error %d fetching %s\r\n", err, url.c_str());
            return err;
        }
    }
}

esp_err_t HttpRequests::DownloadHandler(esp_http_client_event *ev)
{
    auto info = (struct DownloadInfo *)ev->user_data;

    if (ev->event_id == HTTP_EVENT_ON_DATA)
    {
        std::vector<uint8_t> data;
        data.assign((uint8_t *)ev->data, (uint8_t *)ev->data + ev->data_len);
        info->data.insert(info->data.end(), data.begin(), data.end());
    }
    return ESP_OK;
}

