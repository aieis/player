#include "graph.h"

#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <iterator>
#include <list>

Graph::Graph(size_t isize, double iymn, double iymx)
{
    count = isize;
    index = 0;
    pos = 0;

    ymin = iymn;
    ymax = iymx;

    xs.resize(count + 1, 0);
    ys.resize(count + 1, 0);

    xs_rel.resize(count, 0);
    ys_rel.resize(count, 0);
}

void Graph::add(double x, double y)
{
    xs[pos] = x;
    ys[pos] = y;
    pos = (pos + 1) % count;
}

void Graph::draw(std::string pname, float width, float height, double elapsed_time)
{
    int lpos = pos;
    for (size_t j = 0; j < count; j++) {
        size_t idx = (j + lpos) % count;
        xs_rel[j] = xs[idx] - elapsed_time;
        ys_rel[j] = ys[idx];
    }

    ImPlot::SetNextAxesLimits(-120, 0, ymin, ymax);
    if(ImPlot::BeginPlot(pname.c_str(), {width, height})) {
        ImPlot::PlotLine(pname.c_str(), xs_rel.data(), ys_rel.data(), count);
        ImPlot::EndPlot();
    }
}
