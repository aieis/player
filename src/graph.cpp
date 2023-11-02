#include "graph.h"

#include "imgui.h"
#include "implot.h"

Graph::Graph(size_t isize, double iymn, double iymx)
{
    count = isize;
    index = 0;
    size = count * 1.5;

    ymin = iymn;
    ymax = iymx;

    xs.reset(reinterpret_cast<double*>(malloc(sizeof(double) * size)));
    ys.reset(reinterpret_cast<double*>(malloc(sizeof(double) * size)));

    xs_rel.reset(reinterpret_cast<double*>(malloc(sizeof(double) * count)));
}

void Graph::add(double x, double y)
{
    if (index == size) {
        memcpy(xs.get(), xs.get() + (size - count), count);
        memcpy(ys.get(), ys.get() + (size - count), count);
        index = count;
    }

    xs[index] = x;
    ys[index] = y;

    index += 1;
}

void Graph::draw(std::string pname, float width, float height)
{
    double totalTime = 0;
    if (index > 0) {
        totalTime = xs[index - 1];
    }

    int fv = std::min(index - 1, count);
    size_t offset = index > count? index - count : 0;

    for (int j = 0; j < fv; j++) {
        xs_rel[j] = (xs.get() + offset)[j] - totalTime;
    }

    double* ys_rel = ys.get() + offset;

    ImPlot::SetNextAxesLimits(-60, 0, ymin, ymax);
    ImPlot::BeginPlot(pname.c_str(), {width, height});
    ImPlot::PlotLine(pname.c_str(), xs_rel.get(), ys_rel, fv);
    ImPlot::EndPlot();
}
