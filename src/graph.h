#include <stdlib.h>
#include <memory>
#include <string>
#include <vector>

class Graph {
    std::vector<double> xs;
    std::vector<double> ys;

    std::vector<double> xs_rel;

    size_t index;
    size_t count;
    size_t size;

    double ymin;
    double ymax;

 public:
    Graph(size_t isize, double ymin = 0.0, double ymax = 1.0);
    void add(double x, double y);
    void draw(std::string pname, float width, float height, double elapsed_time);
};
