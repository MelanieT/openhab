//
// Created by Melanie on 26/01/2025.
//

#include "OpenHabObject.h"
#include "OpenHab.h"

OpenHabObject::OpenHabObject(OpenHabObject *parent, const json& data) :
    m_parent(parent)
{
    Type type = parent == nullptr ? Type::Root : OpenHab::mapType(data);

    if (type == Type::Root)
    {
        m_widgetData = {
            .widgetId = data.value("id", ""),
            .type = type,
            .label = data.value("title", ""),
            .item = {
                .type = ItemType::UnknownItem
            }
        };
    }
    else
    {
        m_widgetData = {
            .widgetId = data.value("widgetId", ""),
            .type = type,
            .visibility = data.value("visibility", false),
            .label = data.value("label", ""),
            .labelSource = data.value("labelSource", "SITEMAP_WIDGET"),
            .icon = data.value("icon", ""),
            .staticIcon = data.value("staticIcon", false),
            .pattern = data.value("pattern", ""),
            .unit = data.value("unit", ""),
            .leaf = data.value("leaf", false),
        };
        if (data.contains("mappings"))
        {
            for (const auto& m : data["mappings"])
            {
                m_widgetData.mappings.push_back(OpenHabMapping{m.value("command", ""), m.value("label", "")});
            }
        }
        if (data.contains("parent"))
        {
            m_widgetData.parent = {
                .id = data["parent"].value("id", ""),
                .title = data["parent"].value("title", ""),
                .link = data["parent"].value("link", ""),
            };
        }
        if (data.contains("item"))
        {
            auto& item = data["item"];
            m_widgetData.item = {
                .link = item.value("link", ""),
                .state = item.value("state", "NULL"),
                .type = OpenHab::mapItemType(data),
                .name = item.value("name", ""),
                .label = item.value("label", ""),
                .category = item.value("category", ""),
                .unitSymbol = item.value("unitSymbol", "")
            };
            if (item.contains("stateDescription"))
            {
                auto& stateDescription = item["stateDescription"];
                m_widgetData.item.stateDescription = {
                    .step = stateDescription.value("step", 1.0f),
                    .pattern = stateDescription.value("pattern", ""),
                    .readOnly = stateDescription.value("readOnly", false),
                };
                if (stateDescription.contains("options"))
                {
                    m_widgetData.item.stateDescription.options = stateDescription["options"].get<vector<string>>();
                }
            }
        }
        if (data.contains("linkedPage"))
        {
            const auto& linkedPage = data["linkedPage"];
            m_widgetData.linkedPage = {
                .id = linkedPage.value("id", ""),
                .title = linkedPage.value("title", ""),
                .icon = linkedPage.value("icon", ""),
                .link = linkedPage.value("link", ""),
                .leaf = linkedPage.value("leaf", false)
            };
        };
    }

    printf("Created object:\r\n");
    printf("  Widget id '%s'\r\n", m_widgetData.widgetId.c_str());
    printf("  Label id '%s'\r\n", m_widgetData.label.c_str());
}

void OpenHabObject::handleEvent(string target, json& evt)
{

}

void OpenHabObject::deleteChildren()
{
    for (auto child : m_children)
    {
        child->deleteChildren();

    }
    m_children.clear();
}
