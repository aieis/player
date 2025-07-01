#include "list.h"

#include "imgui.h"
#include <algorithm>

ListView::ListView(size_t isize)
{
    pos = 0;
    size = isize;
}

void ListView::add(std::string item)
{
    items.insert(items.begin(), item);
    if (items.size() > size) {
        items[pos] = item;
        pos = (pos + 1) % size;
    }
}

void ListView::draw(std::string pname, float width, float height)
{
    std::vector<const char*> citems;
    for (size_t i = 0; i < items.size(); i++) {
        citems.push_back(items[(i + pos) % size].c_str());
    }

    int current_item = 0;
    double hit = (height - 5.0f) / ImGui::GetTextLineHeightWithSpacing() - 1;
    ImGui::Text("%s", pname.c_str());
    ImGui::BeginListBox(("##" + pname).c_str(), ImVec2(width, height));
    ImGui::ListBox(("##" + pname).c_str(), &current_item, citems.data(), citems.size(), hit);
    ImGui::EndListBox();
}
