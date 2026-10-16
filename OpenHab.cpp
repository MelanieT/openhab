//
// Created by Melanie on 25/01/2025.
//

#include "OpenHab.h"

#include <utility>
#include <vector>
#include <regex>
#include "esp_http_client.h"
#include "HttpRequests.h"

list<OpenHabObject *> OpenHab::m_eventHandlers;
mutex OpenHab::m_eventHandlersLock;

static vector<string> split (const string &s, char delim) {
    vector<string> result;
    stringstream ss (s);
    string item;

    while (getline (ss, item, delim)) {
        result.push_back (item);
    }

    return result;
}

OpenHab::OpenHab() :
    m_eventUrl(nullptr),
    m_handle(nullptr)
{

}

OpenHab::OpenHab(string baseUrl) :
    m_baseUrl(move(baseUrl)),
    m_eventUrl(nullptr),
    m_handle(nullptr)
{

}

OpenHab::OpenHab(string baseUrl, string apiToken) :
    m_baseUrl(move(baseUrl)),
    m_apiToken(std::move(apiToken)),
    m_eventUrl(nullptr),
    m_handle(nullptr)
{
    HttpRequests::setApiToken(m_apiToken); // apiToken itself has been moved from
}

void OpenHab::connectEventChannel()
{
    BaseType_t err;
    while ((err = xTaskCreatePinnedToCore(OpenHab::openHabEventTask, "event_task", 4096, this, 3, (TaskHandle_t *)&m_eventTask, 0)) != pdPASS)
    {
        m_eventTask = nullptr;
        printf("Failed to create event task, err %d\r\n", err);
        printf("Heap free %ld min %ld lblock %d\r\n", esp_get_free_heap_size(), esp_get_minimum_free_heap_size(),
               heap_caps_get_largest_free_block(0));
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void OpenHab::disconnectEventChannel()
{
    printf("Cancelling event stream request\r\n");
    m_abort = true;
    bool cancelled = false;
    TickType_t lastCancel = 0;
    while (m_eventTask)
    {
        // At most once a second: each cancel closes and reopens the connection
        if (!cancelled || xTaskGetTickCount() - lastCancel >= pdMS_TO_TICKS(1000))
        {
            m_handleLock.lock();
            if (m_handle)
                esp_http_client_cancel_request(m_handle);
            m_handleLock.unlock();
            cancelled = true;
            lastCancel = xTaskGetTickCount();
        }
        // Must yield. This runs on the system event task (prio 20, core 0); spinning
        // starved the event task (prio 3, core 0), so it never exited.
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    m_abort = false;
    free(m_eventUrl);
    m_eventUrl = nullptr; // Else a second disconnect frees it again

//    if (m_eventTask)
//    {
//        vTaskDelete(m_eventTask);
//        free(m_eventUrl);
//        m_eventUrl = nullptr;
//    }
}

int OpenHab::openHabEvent(esp_http_client_event *ev)
{
    static string eventBuffer;

    if (ev->event_id == HTTP_EVENT_ON_DATA && ev->data_len > 0) // Empty buffer would be indexed at -1 below
    {
        for (int i = 0; i < ev->data_len; i++)
            eventBuffer.push_back(((char *) ev->data)[i]);

//        printf("Event buffer now %s\r\n", eventBuffer.c_str());

        // Split into complete lines by hand, the rest stays buffered. Same result as
        // the std::regex split this replaces, which was heavy on a 4k stack at ~17
        // events per second.
        vector<string> pieces;
        size_t start = 0;
        size_t nl;
        while ((nl = eventBuffer.find('\n', start)) != string::npos)
        {
            size_t end = nl;
            if (end > start && eventBuffer[end - 1] == '\r')
                end--;
            pieces.push_back(eventBuffer.substr(start, end - start));
            start = nl + 1;
        }
        eventBuffer.erase(0, start);

        if (!pieces.empty())
        {
            for (auto &piece: pieces)
            {
                if (piece.starts_with("data:"))
                {
                    json msg;
                    try
                    {
                        size_t idx;
                        if ((idx = piece.find("event:")) != -1)
                        {
                            piece = piece.substr(0, idx);
                        }
                        msg = json::parse(piece.substr(6));
                    }
                    catch (exception &e)
                    {
                        printf("Exception: %s\r\n", e.what());
                        printf("JSON: %s\r\n", piece.c_str());
                        return ESP_OK;
                    }

                    if (msg.contains("topic"))
                    {
                        auto topic = msg["topic"].get<string>();
                        auto pathParts = split(topic, '/');
                        if (pathParts.size() < 3)
                            continue;
                        msg["target"] = pathParts[2];
                        msg["path_parts"] = pathParts;
                    }
                    else
                    {
                        continue;
                    }

                    if (msg.contains("payload"))
                    {
                        // Uncaught, a bad payload aborts the whole device
                        try
                        {
                            json payload = json::parse(msg["payload"].get<string>());
                            msg["payload"] = payload;
                        }
                        catch (exception &e)
                        {
                            printf("Bad event payload: %s\r\n", e.what());
                            continue;
                        }
                    }

//                    printf("JSON: %s\r\n", msg.dump().c_str());

//        printf("Heap free %ld min %ld lblock %d\r\n", esp_get_free_heap_size(), esp_get_minimum_free_heap_size(),
//               heap_caps_get_largest_free_block(0));

                    ((OpenHab *) ev->user_data)->sendStatusUpdate(msg);

                }
            }
        }
    }

    return ESP_OK;
}

void OpenHab::openHabEventTask(void *user)
{
    auto openhab = (OpenHab *)user;
    esp_http_client_config_t conf;
    memset(&conf, 0, sizeof(conf));
    openhab->m_eventUrl = strdup((openhab->m_baseUrl + "/rest/events?topics=openhab/items/*").c_str());

    conf = {
        .url = openhab->m_eventUrl,
        .auth_type = HTTP_AUTH_TYPE_NONE,
        .user_agent = "ESP32 Mozilla Compatible",
        .method = HTTP_METHOD_GET,
        .event_handler = OpenHab::openHabEvent,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .buffer_size = 2048,
        .user_data = user,
        .keep_alive_enable = true
    };

    printf("Connecting to %s\r\n", conf.url);

    while (true)
    {
        esp_http_client *handle = esp_http_client_init(&conf);
        openhab->m_handleLock.lock();
        openhab->m_handle = handle;
        openhab->m_handleLock.unlock();

        esp_err_t err = esp_http_client_perform(handle);

        // Detach before freeing, so a concurrent cancel can't use a freed client
        openhab->m_handleLock.lock();
        openhab->m_handle = nullptr;
        openhab->m_handleLock.unlock();
        esp_http_client_cleanup(handle);

        printf("http_client_perform exited with code %d\r\n", err);
        if (openhab->m_abort)
        {
            openhab->m_eventTask = nullptr;
            vTaskDelete(nullptr);
        }

        if (err == ESP_OK) {
            continue;
        }
        if (err == ESP_ERR_INVALID_ARG)
        {
            // Likely a DNS error
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            continue;
        }
        printf("Event channel disconnected, error %d, retrying\r\n", err);
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Don't hammer a server that refuses us
    }

    vTaskDelete(nullptr);
}

void OpenHab::sendStatusUpdate(json& msg)
{
    auto target = msg["target"].get<string>();

    // Dispatch from a copy. Flyouts register and unregister widgets from the UI task
    // while this runs, which changed the list under the loop. The lock is not held
    // while handlers run, so it can't interfere with their own locking.
    m_eventHandlersLock.lock();
    vector<OpenHabObject *> handlers(m_eventHandlers.begin(), m_eventHandlers.end());
    m_eventHandlersLock.unlock();

    for (auto o : handlers)
    {
        o->handleEvent(target, msg);
    }
}

int OpenHab::loadSitemap(const string& url, OpenHabFactory *factory, const function<void(shared_ptr<OpenHabObject>)>& func, void *userData)
{
    return HttpRequests::getUrl(url, [this, factory, func, userData](vector<uint8_t>& data){ this->handleSitemap(data, factory, func, userData);});
}

void OpenHab::handleSitemap(vector<uint8_t>& data, OpenHabFactory *factory, const function<void(shared_ptr<OpenHabObject>)>& func, void *userData)
{
    json sitemap;

    try
    {
        sitemap = json::parse(data);
    }
    catch (...)
    {
        printf("Failed to load sitemap\r\n");
        (func)(shared_ptr<OpenHabObject>());
        return;
    }

    shared_ptr<OpenHabObject> root;
    try
    {
        if (factory)
            root = shared_ptr<OpenHabObject>(factory->createObject(nullptr, sitemap, Type::Root, userData));
        else
            root = make_shared<OpenHabObject>(nullptr, sitemap);

        addChildrenRecursive(root, sitemap, factory);
        printf("Done adding all children\r\n");
    }
    catch (...)
    {
        (func)(shared_ptr<OpenHabObject>());
        return;
    }

    (func)(root);
}

int OpenHab::loadSitemapsList(const function<void(vector<Sitemap>)>& func)
{
    return HttpRequests::getUrl(m_baseUrl + "/rest/sitemaps", [this, func](auto d){((OpenHab *)this)->handleSitemaps(d, func);});
}

void OpenHab::handleSitemaps(vector<uint8_t> &data, const function<void(vector<Sitemap>)>& func)
{
    json sitemaps;
    vector<Sitemap> ret;

    try
    {
        sitemaps = json::parse(data);
    }
    catch (...)
    {
        printf("Failed to load sitemap list\r\n");
        (func)(ret);
        return;
    }

    try
    {
        for (auto map: sitemaps)
        {
            Sitemap s;
            if (map.contains("name") && map.contains("label") & map.contains("homepage"))
            {
                s.name = map["name"].get<string>();
                s.label = map["label"].get<string>();
                s.homepage = map["homepage"]["link"].get<string>();

                ret.emplace_back(s);
            }
        }
    }
    catch (...) {};

    (func)(ret);
}

Type OpenHab::mapType(const json& data)
{
    string strtype = data.value("type", "Unknown");
    if (strtype == "Switch")
        return Type::Switch;
    else if (strtype == "Slider")
        return Type::Slider;
    else if (strtype == "Text")
        return Type::Text;
    else if (strtype == "Colorpicker")
        return Type::Colorpicker;
    else if (strtype == "Group")
        return Type::Group;
    else if (strtype == "Rollershutter")
        return Type::Rollershutter;
    else if (strtype == "Frame")
        return Type::Frame;

    return Type::Unknown;
}

void OpenHab::addChildrenRecursive(shared_ptr<OpenHabObject>& current, const json &data, OpenHabFactory *factory)
{
    static int recursion = 0;

    recursion++;
    for (const auto& w : data["widgets"])
    {
        Type type = OpenHab::mapType(w);

        shared_ptr<OpenHabObject> child;
        if (factory)
            child = shared_ptr<OpenHabObject>(factory->createObject(current.get(), w, type, nullptr));
        else
            child = make_shared<OpenHabObject>(current.get(), w);

        printf("Adding child to current\r\n");
        current->addChild(child);

        printf("Adding children recursively level %d\r\n", recursion);
        addChildrenRecursive(child, w, factory);
        printf("Done adding children level %d\r\n", recursion);
    }
    recursion--;
}

ItemType OpenHab::mapItemType(const json &data)
{
    if (!data.contains("item"))
        return ItemType::UnknownItem;
    else if (!data["item"].contains("type"))
        return ItemType::UnknownItem;
    else
    {
        auto pieces = split(data["item"].value("type", "Unknown"), ':');
        const auto& type = pieces[0];
        if (type == "Switch")
            return ItemType::SwitchItem;
        else if (type == "Dimmer")
            return ItemType::DimmerItem;
        else if (type == "Color")
            return ItemType::ColorItem;
        else if (type == "Group")
            return ItemType::GroupItem;
        else if (type == "Number")
            return ItemType::NumberItem;
        else if (type == "String")
            return ItemType::StringItem;
    }
    return ItemType::UnknownItem;
}

void OpenHab::registerEventHandler(OpenHabObject *obj)
{
    lock_guard<mutex> guard(m_eventHandlersLock);
    if (std::find(m_eventHandlers.begin(), m_eventHandlers.end(),obj) == m_eventHandlers.end())
        m_eventHandlers.push_back(obj);
}

void OpenHab::unregisterEventHandler(OpenHabObject *obj)
{
    lock_guard<mutex> guard(m_eventHandlersLock);
    m_eventHandlers.remove(obj);
}

