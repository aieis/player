#include "list.h"

#include "imgui.h"
#include <algorithm>

ListView::ListView(size_t isize)
{
    size = isize;
}

void ListView::add(std::string item)
{
    items.insert(items.begin(), item);
    if (items.size() > size) {
        items.pop_back();
    }
}

void ListView::draw(std::string pname, float width, float height)
{
    std::vector<const char*> citems;
    for (auto&& s : items) {
        citems.push_back(s.c_str());
    }

    int current_item = 0;
    double hit = (height - 5.0f) / ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginListBox(pname.c_str(), ImVec2(width, height));
    ImGui::ListBox(pname.c_str(), &current_item, citems.data(), citems.size(), hit);
    
    ImGui::EndListBox();
}
