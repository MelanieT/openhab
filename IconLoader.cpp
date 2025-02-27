//
// Created by Melanie on 04/01/2025.
//

#include <memory>
#include <map>
#include <utility>
#include <algorithm>
#include "IconLoader.h"
#include "esp_task.h"
#include "PNGdec.h"

set<IconReceiver *> IconLoader::m_receivers;
mutex IconLoader::m_receiverLock;
list<shared_ptr<IconInfo>> IconLoader::m_icons;
mutex IconLoader::m_netRequestQueueLock;
mutex IconLoader::m_iconLock;
list<shared_ptr<IconInfo>> IconLoader::m_netRequestQueue;
string IconLoader::m_baseUrl;

void IconLoader::init(string baseUrl)
{
    m_baseUrl = std::move(baseUrl);
    xTaskCreatePinnedToCore(IconLoader::NetworkQueueTask, "image_load", 4096, nullptr, 1, nullptr, 0);
}

void IconLoader::fetchIcon(const string &name, int size)
{
    m_iconLock.lock();

    if (name == "none")
        return;

    auto icon = findIcon(name, size, size);

    if (icon != nullptr)
    {
        m_iconLock.unlock();

        if (icon->status_code == 200 && !icon->pngdata.empty())
            Dispatch(icon);

        return;
    }
    else
    {
        m_iconLock.unlock();
    }

    auto info = make_shared<IconInfo>();
    info->name = name;
    info->width = size;
    info->height = size;

    m_netRequestQueueLock.lock();
//    if (m_netRequestQueue.empty())
//        xTaskCreate(IconLoader::NetworkQueueTask, "image_load", 4096, nullptr, 3, nullptr);
    m_netRequestQueue.push_back(info);
    m_netRequestQueueLock.unlock();

}

void IconLoader::registerReceiver(IconReceiver *inst)
{
    m_receiverLock.lock();
    if (!m_receivers.contains(inst))
        m_receivers.insert(inst);
    m_receiverLock.unlock();
}

void IconLoader::unregisterReceiver(IconReceiver *inst)
{
    m_receiverLock.lock();
    m_receivers.erase(inst);
    m_receiverLock.unlock();
}

int IconLoader::ImageLoadEvent(esp_http_client_event *ev)
{
    auto *info = (IconInfo *)ev->user_data;
    if (ev->event_id == HTTP_EVENT_ON_HEADER)
    {
//        if (!strcmp(strlwr(ev->header_key), "content-type"))
//            info->content_type = ev->header_value;
//        printf("key %s value %s\r\n", ev->header_key, ev->header_value);
    }
    else if (ev->event_id == HTTP_EVENT_ON_DATA)
    {
        vector<uint8_t> data;
        data.assign((uint8_t *)ev->data, (uint8_t *)ev->data + ev->data_len);
        info->data.insert(info->data.end(), data.begin(), data.end());
    }

    return ESP_OK;
}

void IconLoader::NetworkQueueTask(void *user)
{
    m_netRequestQueueLock.lock();
    while (true) {
        if (m_netRequestQueue.empty())
        {
            m_netRequestQueueLock.unlock();
            vTaskDelay(10 / portTICK_PERIOD_MS);
            m_netRequestQueueLock.lock();
            continue;
        }
        auto info = m_netRequestQueue.front();
        m_netRequestQueue.pop_front();
        m_netRequestQueueLock.unlock();

        size_t len = m_baseUrl.length() + info->name.length() + 20; // Reasonably safe
        char buf[len];

        // Only square icons for now
        snprintf(buf, len, "%s%s&size=%d", m_baseUrl.c_str(), info->name.c_str(), info->width);

//        auto url = string("http://openhab.t-data.com:5050/?image=") +  info->name;

        // Process fetch
        printf("Request to load %s\r\n", buf);

        esp_err_t err;

        esp_http_client_config_t conf;
        memset(&conf, 0, sizeof(conf));
        conf = {
            .url = buf,
            .auth_type = HTTP_AUTH_TYPE_NONE,
            .user_agent = "ESP32 Mozilla Compatible",
            .method = HTTP_METHOD_GET,
            .event_handler = IconLoader::ImageLoadEvent,
            .transport_type = HTTP_TRANSPORT_OVER_TCP,
            .buffer_size = 8192,
            .user_data = info.get(),
            .keep_alive_enable = true
        };

        esp_http_client *handle = esp_http_client_init(&conf);

        err = esp_http_client_perform(handle);

        info->status_code = esp_http_client_get_status_code(handle);

        esp_http_client_close(handle);

        printf("%s: err is %d, status %d, data len %d\r\n", info->name.c_str(), err, info->status_code, info->data.size());

        m_iconLock.lock();
        m_icons.push_back(info);
        m_iconLock.unlock();
        if (info->status_code == 200)
        {
            decodeIcon(info);
            if (!info->pngdata.empty())
                Dispatch(info.get());
        }

        m_netRequestQueueLock.lock();
//        if (m_netRequestQueue.empty()) {
//            m_netRequestQueueLock.unlock();
//            vTaskDelete(nullptr);
//            return;
//        }
    }
}

void IconLoader::Dispatch(IconInfo *info)
{
    m_receiverLock.lock();
    for (auto rec : m_receivers)
        rec->receiveIcon(info);
    m_receiverLock.unlock();
}

void IconLoader::addIconFromData(string name, const uint8_t* data, int width, int height)
{
    m_iconLock.lock();

    int size = width * height * 2;

    auto icon = make_shared<IconInfo>();
    icon->status_code = 200;
    icon->name = name;
    icon->hasAlpha = false;
    icon->width = width;
    icon->height = height;
    icon->pngdata.insert(icon->pngdata.end(), data, data + size);
    m_icons.push_back(icon);

    printf("Icon %s inserted, data size %d\r\n", icon->name.c_str(), icon->pngdata.size());

    m_iconLock.unlock();
}

vector<uint8_t> IconLoader::getIcon(const string& name, int width, int height)
{
    m_iconLock.lock();
    auto icon = findIcon(name, width, height);
    m_iconLock.unlock();
    if (icon == nullptr)
        return vector<uint8_t>{};

    return icon->pngdata;
}

void IconLoader::decodeIcon(const shared_ptr<IconInfo>& info)
{
    if (!info->data.empty())
    {
        auto png = make_shared<PNG>();
        auto decoded = vector<uint16_t>();

        struct callbackData
        {
            shared_ptr<PNG> png;
            shared_ptr<IconInfo> info;
        } cbd{
            .png = png,
            .info = info
        };
        png->openRAM((uint8_t *)(info->data.data()), (int) info->data.size(), [](auto pngdraw) {
            auto cbd = (struct callbackData *) pngdraw->pUser;
            vector<uint8_t> row(pngdraw->iWidth * 2);

            cbd->png->getLineAsRGB565(pngdraw, (uint16_t *)row.data(), 0, 0);

            if (!cbd->info->hasAlpha)
            {
                cbd->info->pngdata.insert(cbd->info->pngdata.end(), row.begin(), row.end());
            }
            else
            {
                auto it = row.begin();
                for (int i = 0 ; i < row.size() ; i += 2 , it += 2)
                {
                    cbd->info->pngdata.insert(cbd->info->pngdata.end(), it, it + 2);
                    cbd->info->pngdata.push_back(pngdraw->pPixels[3 + i * 2]);
                }
            }
        });

        info->hasAlpha = png->hasAlpha();
        info->width = png->getWidth();
        info->height = png->getHeight();

        png->decode(&cbd, 0);

        printf("Image %s has alpha %d\r\n", info->name.c_str(), info->hasAlpha);

        info->data.clear();
    }
}

IconInfo *IconLoader::findIcon(const string& name, int width, int height)
{
    for (auto x : m_icons)
    {
        if (x->name == name && x->width == width && x->height == height)
            return x.get();
    }

    return nullptr;
}
