#include "graph.h"

#include <algorithm>
#include <list>

Graph::Graph(size_t isize, double iymn, double iymx)
{
    count = isize;
    index = 0;
    size = count * 1.5;

    ymin = iymn;
    ymax = iymx;

    xs.resize(count + 1, 0);
    ys.resize(count + 1, 0);

    xs_rel.resize(count + 1, 0);
}

void Graph::add(double x, double y)
{
    xs.erase(xs.begin());
    ys.erase(ys.begin());

    xs.push_back(x);
    ys.push_back(y);
}

void Graph::draw(std::string pname, float width, float height)
{
}
