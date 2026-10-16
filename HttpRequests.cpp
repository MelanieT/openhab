//
// Created by Melanie on 26/01/2025.
//

#include <esp_err.h>
#include <esp_http_client.h>
#include <functional>
#include "HttpRequests.h"

#include <list>

mutex HttpRequests::m_httpLock;
string HttpRequests::m_apiToken;

std::list<QueuedRequestInfo *> HttpRequests::m_queuedRequests;
std::mutex HttpRequests::m_queuedRequestsLock;

TaskHandle_t HttpRequests::m_queueTask = nullptr;

void HttpRequests::requestHandler(void *arg)
{
    m_queuedRequestsLock.lock();

    while (true)
    {
        if (m_queuedRequests.empty())
        {
            m_queuedRequestsLock.unlock();
            vTaskDelay(20 / portTICK_PERIOD_MS);
            m_queuedRequestsLock.lock();
            continue;
        }

        auto info = m_queuedRequests.front();
        m_queuedRequests.pop_front();
        m_queuedRequestsLock.unlock();

        postUrl(info->url, info->data, info->func);

        delete info;

        m_queuedRequestsLock.lock();
    }
}

esp_err_t HttpRequests::queuePostUrl(const std::string& url, std::vector<uint8_t>& data, const function<void(vector<uint8_t>&)>& func)
{
    if (m_queueTask == nullptr)
    {
        xTaskCreate(HttpRequests::requestHandler, "post_handler", 6144, nullptr, 1, &m_queueTask);
    }

    auto *info = new QueuedRequestInfo{url, data, func};
    m_queuedRequestsLock.lock();
    m_queuedRequests.emplace_back(info);
    // No second xTaskCreate here: the handler never exits, so that started (and
    // leaked) another task with a 6k stack on nearly every request
    m_queuedRequestsLock.unlock();

    return ESP_OK;
}

esp_err_t HttpRequests::postUrl(const std::string& url, std::vector<uint8_t>& data, const function<void(vector<uint8_t>&)>& func)
{
    esp_err_t err;

    m_httpLock.lock();

    struct DownloadInfo info;

    printf("POST payload length %d, url %s\r\n", data.size(), url.c_str());
    if (data.size() > 0)
    {
        printf("Posting %.*s to %s\r\n", data.size(), (const char *)data.data(), url.c_str());
    }

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
        if (handle == nullptr)
        {
            m_httpLock.unlock();
            printf("Failed to init http client for %s\r\n", url.c_str());
            return ESP_ERR_NO_MEM;
        }
        esp_http_client_set_post_field(handle, (const char *) data.data(), (int) data.size());
        esp_http_client_set_header(handle, "Content-type", "text/plain");
        esp_http_client_set_header(handle, "Accept", "application/json");
        if (!m_apiToken.empty()) {
            // Not static, it kept the first token forever. The value is copied.
            string auth = "Bearer " + m_apiToken;
            esp_http_client_set_header(handle, "Authorization", auth.c_str());
        }

        err = esp_http_client_perform(handle);

        esp_http_client_cleanup(handle);

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
        if (handle == nullptr)
        {
            m_httpLock.unlock();
            printf("Failed to init http client for %s\r\n", url.c_str());
            return ESP_ERR_NO_MEM;
        }
        if (!m_apiToken.empty()) {
            // Not static, it kept the first token forever. The value is copied.
            string auth = "Bearer " + m_apiToken;
            esp_http_client_set_header(handle, "Authorization", auth.c_str());
        }

        err = esp_http_client_perform(handle);

        esp_http_client_cleanup(handle);

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

