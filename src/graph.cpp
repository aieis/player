#include "graph.h"

#include "imgui.h"
#include "implot.h"
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

void Graph::draw(std::string pname, float width, float height, double elapsed_time)
{
    auto lxs = xs;
    auto lys = ys;

    double totalTime = elapsed_time;
    
    for (int j = 0; j < count; j++) {
        xs_rel[j] = lxs[j] - totalTime;
    }

    double* ys_rel = lys.data();

    ImPlot::SetNextAxesLimits(-60, 0, ymin, ymax);
    if(ImPlot::BeginPlot(pname.c_str(), {width, height})) {
        ImPlot::PlotLine(pname.c_str(), xs_rel.data(), ys_rel, count);
        ImPlot::EndPlot();
    } 
}
