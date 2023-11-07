#include <stdlib.h>
#include <memory>
#include <string>
#include <vector>

class ListView {
    size_t size;
    std::vector<std::string> items;
 public:
    ListView(size_t isize);
    void add(std::string item);
    void draw(std::string pname, float width, float height);
};
