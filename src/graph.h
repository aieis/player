#include <stdlib.h>
#include <memory>
#include <string>

class Graph {
    std::unique_ptr<double[]> xs;
    std::unique_ptr<double[]> ys;

    std::unique_ptr<double[]> xs_rel;

    size_t index;
    size_t count;
    size_t size;

    double ymin;
    double ymax;
    
 public:
    Graph(size_t isize, double ymin = 0.0, double ymax = 1.0);
    void add(double x, double y);
    void draw(std::string pname, float width, float height);
};
