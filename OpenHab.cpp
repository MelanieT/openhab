//
// Created by Melanie on 25/01/2025.
//

#include "OpenHab.h"

#include <utility>
#include <vector>
#include <regex>
#include "esp_http_client.h"
#include "HttpRequests.h"

static vector<string> split (const string &s, char delim) {
    vector<string> result;
    stringstream ss (s);
    string item;

    while (getline (ss, item, delim)) {
        result.push_back (item);
    }

    return result;
}

list<OpenHabObject *> OpenHab::m_eventHandlers;

OpenHab::OpenHab() :
    m_eventUrl(nullptr)
{

}

OpenHab::OpenHab(string baseUrl) :
    m_baseUrl(move(baseUrl)),
    m_eventUrl(nullptr)
{

}

void OpenHab::connectEventChannel()
{
    BaseType_t err;
    while ((err = xTaskCreatePinnedToCore(OpenHab::openHabEventTask, "event_task", 4096, this, 3, &m_eventTask, 0)) != pdPASS)
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
    if (m_eventTask)
    {
        vTaskDelete(m_eventTask);
        free(m_eventUrl);
        m_eventUrl = nullptr;
    }
}

int OpenHab::openHabEvent(esp_http_client_event *ev)
{
    static string eventBuffer;

    if (ev->event_id == HTTP_EVENT_ON_DATA)
    {
        for (int i = 0; i < ev->data_len; i++)
            eventBuffer.push_back(((char *) ev->data)[i]);

//        printf("Event buffer now %s\r\n", eventBuffer.c_str());

        const regex ws_re("\\r?\\n"); // whitespace
        vector<string> pieces;

        copy(sregex_token_iterator(eventBuffer.begin(), eventBuffer.end(), ws_re, -1),
             sregex_token_iterator(),
             back_inserter(pieces));

        if (eventBuffer[eventBuffer.length() - 1] == '\n')
        {
            eventBuffer.clear();
        }
        else
        {
            eventBuffer = pieces[pieces.size() - 1];
            pieces.pop_back();
        }

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
                        msg["target"] = pathParts[2];
                        msg["path_parts"] = pathParts;
                    }
                    else
                    {
                        continue;
                    }

                    if (msg.contains("payload"))
                    {
                        json payload = json::parse(msg["payload"].get<string>());
                        msg["payload"] = payload;
                    }

//                    printf("JSON: %s\r\n", msg.dump().c_str());

//        printf("Heap free %ld min %ld lblock %d\r\n", esp_get_free_heap_size(), esp_get_minimum_free_heap_size(),
//               heap_caps_get_largest_free_block(0));

                    ((OpenHab *) ev->user_data)->sendStatusUpdate(msg);

                }
            }
        }
    }



//        ((char *)ev->data)[ev->data_len] = 0;
//        string message = ((char *)ev->data);
//        const regex ws_re("\\r?\\n"); // whitespace
//        vector<string> pieces;
//
//        copy(sregex_token_iterator(message.begin(), message.end(), ws_re, -1),
//                  sregex_token_iterator(),
//                  back_inserter(pieces));
//
//        if (pieces[0] != "event: message")
//            return ESP_OK;
//
//        if (pieces.size() < 2 || pieces[1].length() < 7)
//            return ESP_OK;
//
//        string json_str = pieces[1].substr(6);
//
//        json msg;
//        try
//        {
//            msg = json::parse(json_str);
//        }
//        catch (exception& e)
//        {
//            printf("Exception: %s\r\n", e.what());
//            printf("JSON: %s\r\n", json_str.c_str());
//            return ESP_OK;
//        }
//
//        auto topic = msg["topic"].get<string>();
//        auto pathParts = split(topic, '/');
//        msg["target"] = pathParts[2];
//        msg["path_parts"] = pathParts;
//
//        if (msg.contains("payload"))
//        {
//            json payload = json::parse(msg["payload"].get<string>());
//            msg["payload"] = payload;
//        }
//
////        printf("JSON: %s\r\n", msg.dump().c_str());
//
////        printf("Heap free %ld min %ld lblock %d\r\n", esp_get_free_heap_size(), esp_get_minimum_free_heap_size(),
////               heap_caps_get_largest_free_block(0));
//
//        ((OpenHab *)ev->user_data)->sendStatusUpdate(msg);
//    }
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
        esp_err_t err = esp_http_client_perform(handle);
        esp_http_client_close(handle);

        if (err == ESP_OK) {
            continue;
        }
        if (err == ESP_ERR_INVALID_ARG)
        {
            // Likely a DNS error
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            continue;
        }
        printf("Err is %d, retry\r\n", err);
    }

    vTaskDelete(nullptr);
}

void OpenHab::sendStatusUpdate(json& msg)
{
    auto target = msg["target"].get<string>();

    for (auto o : m_eventHandlers)
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
    }
    return ItemType::UnknownItem;
}

void OpenHab::registerEventHandler(OpenHabObject *obj)
{
    if (std::find(m_eventHandlers.begin(), m_eventHandlers.end(),obj) == m_eventHandlers.end())
        m_eventHandlers.push_back(obj);
}

void OpenHab::unregisterEventHandler(OpenHabObject *obj)
{
    m_eventHandlers.remove(obj);
}
