// svgplot.h - very small SVG chart writer used by the benchmark program
// (line charts, scatter/trace charts and grouped bar charts).
#pragma once

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace plot {

struct Series {
    std::string name, color;
    std::vector<std::pair<double, double>> pts;
    bool line = true;      // connect points
    bool markers = true;   // draw point markers
    bool dashed = false;
    bool step = false;     // draw as a step line (value holds until the next point)
    char marker = 'o';     // 'o' circle, 'x' cross, 's' square, '.' small dot
    Series() = default;
    Series(const std::string &n, const std::string &c) : name(n), color(c) {}
};

struct Chart {
    std::string title, xlabel, ylabel;
    std::vector<Series> series;
    double xmin = NAN, xmax = NAN, ymin = 0, ymax = NAN;
    bool logx = false;
    bool logy = false;   // ymin must be > 0
    int width = 760, height = 440;
    std::string legendPos = "right";  // "right" (top right) or "left" (top left)
};

inline std::string esc(const std::string &s) {
    std::string o;
    for (char c : s) {
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else o += c;
    }
    return o;
}

inline double niceStep(double range, int ticks) {
    double raw = range / ticks, mag = std::pow(10, std::floor(std::log10(raw)));
    double r = raw / mag;
    return (r < 1.5 ? 1 : r < 3 ? 2 : r < 7 ? 5 : 10) * mag;
}

inline std::string num(double v) {
    std::ostringstream o;
    if (std::fabs(v) >= 100 || std::fabs(v - std::round(v)) < 1e-9) o << (long long)std::llround(v);
    else if (std::fabs(v) >= 1) o << std::fixed << std::setprecision(1) << v;
    else o << std::setprecision(2) << v;
    return o.str();
}

inline std::string header(int w, int h) {
    std::ostringstream o;
    o << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='0 0 " << w
      << " " << h << "' font-family='Segoe UI, Arial, Helvetica, sans-serif'>\n"
      << "<rect width='100%' height='100%' fill='white'/>\n";
    return o.str();
}

inline void save(const std::string &path, const std::string &svg) {
    std::ofstream f(path);
    f << svg;
}

inline std::string lineChart(const Chart &c) {
    double xmin = c.xmin, xmax = c.xmax, ymin = c.ymin, ymax = c.ymax;
    double dx0 = 1e300, dx1 = -1e300, dy1 = -1e300;
    for (auto &s : c.series)
        for (auto &p : s.pts) {
            dx0 = std::min(dx0, p.first);
            dx1 = std::max(dx1, p.first);
            dy1 = std::max(dy1, p.second);
        }
    if (std::isnan(xmin)) xmin = dx0;
    if (std::isnan(xmax)) xmax = dx1;
    if (std::isnan(ymax)) ymax = dy1 * 1.08;
    if (ymax <= ymin) ymax = ymin + 1;
    if (xmax <= xmin) xmax = xmin + 1;
    int maxNameLen = 0;
    for (auto &s : c.series) maxNameLen = std::max(maxNameLen, (int)s.name.size());
    const bool outside = c.legendPos == "outside";
    const int legendW = outside ? 46 + maxNameLen * 7 : 0;
    const int L = 78, R = 24 + legendW, T = 48, B = 62;
    int pw = c.width - L - R, ph = c.height - T - B;
    auto X = [&](double x) {
        if (c.logx) return L + pw * (std::log2(x) - std::log2(xmin)) / (std::log2(xmax) - std::log2(xmin));
        return L + pw * (x - xmin) / (xmax - xmin);
    };
    auto Y = [&](double y) {
        if (c.logy) {
            y = std::max(y, ymin);
            return T + ph - ph * (std::log10(y) - std::log10(ymin)) / (std::log10(ymax) - std::log10(ymin));
        }
        return T + ph - ph * (y - ymin) / (ymax - ymin);
    };
    std::ostringstream o;
    o << header(c.width, c.height);
    o << "<text x='" << c.width / 2 << "' y='28' text-anchor='middle' font-size='17' font-weight='600' fill='#1f2933'>"
      << esc(c.title) << "</text>\n";
    double ys = niceStep(ymax - ymin, 6);
    std::vector<double> yt;
    if (c.logy)
        for (double y = std::pow(10, std::ceil(std::log10(ymin) - 1e-9)); y <= ymax * 1.0001; y *= 10) yt.push_back(y);
    else
        for (double y = ymin; y <= ymax + 1e-9; y += ys) yt.push_back(y);
    for (double y : yt) {
        o << "<line x1='" << L << "' x2='" << L + pw << "' y1='" << Y(y) << "' y2='" << Y(y)
          << "' stroke='#e4e7eb' stroke-width='1'/>\n";
        o << "<text x='" << L - 8 << "' y='" << Y(y) + 4 << "' text-anchor='end' font-size='12' fill='#52606d'>"
          << (c.logy && y < 0.01 ? [&] { std::ostringstream t; t << y; return t.str(); }() : num(y)) << "</text>\n";
    }
    if (c.logx && xmax / xmin > 50) {
        auto lab = [](double v) {
            std::ostringstream t;
            if (v < 0.01) {
                int e = (int)std::floor(std::log10(v) + 1e-9);
                t << (int)std::lround(v / std::pow(10, e)) << "e" << e;
            } else {
                t << v;
            }
            return t.str();
        };
        for (double d = std::pow(10, std::floor(std::log10(xmin))); d <= xmax * 1.0001; d *= 10)
            for (double m : {1.0, 3.0}) {
                double x = d * m;
                if (x < xmin * 0.999 || x > xmax * 1.001) continue;
                o << "<line x1='" << X(x) << "' x2='" << X(x) << "' y1='" << T << "' y2='" << T + ph
                  << "' stroke='#f0f2f4'/>\n";
                o << "<text x='" << X(x) << "' y='" << T + ph + 18 << "' text-anchor='middle' font-size='12' fill='#52606d'>"
                  << lab(x) << "</text>\n";
            }
    } else if (c.logx) {
        for (double x = xmin; x <= xmax * 1.0001; x *= 2) {
            o << "<line x1='" << X(x) << "' x2='" << X(x) << "' y1='" << T << "' y2='" << T + ph
              << "' stroke='#f0f2f4'/>\n";
            o << "<text x='" << X(x) << "' y='" << T + ph + 18 << "' text-anchor='middle' font-size='12' fill='#52606d'>"
              << num(x) << "</text>\n";
        }
    } else {
        double xs = niceStep(xmax - xmin, 8);
        for (double x = std::ceil(xmin / xs) * xs; x <= xmax + 1e-9; x += xs) {
            o << "<line x1='" << X(x) << "' x2='" << X(x) << "' y1='" << T << "' y2='" << T + ph
              << "' stroke='#f0f2f4'/>\n";
            o << "<text x='" << X(x) << "' y='" << T + ph + 18 << "' text-anchor='middle' font-size='12' fill='#52606d'>"
              << num(x) << "</text>\n";
        }
    }
    o << "<rect x='" << L << "' y='" << T << "' width='" << pw << "' height='" << ph
      << "' fill='none' stroke='#9aa5b1'/>\n";
    o << "<text x='" << L + pw / 2 << "' y='" << c.height - 16 << "' text-anchor='middle' font-size='13' fill='#323f4b'>"
      << esc(c.xlabel) << "</text>\n";
    o << "<text transform='translate(20," << T + ph / 2 << ") rotate(-90)' text-anchor='middle' font-size='13' fill='#323f4b'>"
      << esc(c.ylabel) << "</text>\n";
    o << "<svg x='" << L << "' y='" << T << "' width='" << pw << "' height='" << ph << "' overflow='hidden'>\n";
    for (auto &s : c.series) {
        if (s.line && s.pts.size() > 1) {
            o << "<polyline fill='none' stroke='" << s.color << "' stroke-width='2.2'"
              << (s.dashed ? " stroke-dasharray='7,5'" : "") << " points='";
            for (size_t i = 0; i < s.pts.size(); i++) {
                auto &p = s.pts[i];
                if (s.step && i > 0) o << X(p.first) - L << "," << Y(s.pts[i - 1].second) - T << " ";
                o << X(p.first) - L << "," << Y(p.second) - T << " ";
            }
            o << "'/>\n";
        }
        if (s.markers)
            for (auto &p : s.pts) {
                double x = X(p.first) - L, y = Y(p.second) - T;
                if (s.marker == 'x')
                    o << "<path d='M" << x - 4 << "," << y - 4 << " L" << x + 4 << "," << y + 4 << " M" << x - 4 << ","
                      << y + 4 << " L" << x + 4 << "," << y - 4 << "' stroke='" << s.color << "' stroke-width='1.8'/>\n";
                else if (s.marker == 's')
                    o << "<rect x='" << x - 3.5 << "' y='" << y - 3.5 << "' width='7' height='7' fill='" << s.color << "'/>\n";
                else if (s.marker == '.')
                    o << "<circle cx='" << x << "' cy='" << y << "' r='1.6' fill='" << s.color << "'/>\n";
                else
                    o << "<circle cx='" << x << "' cy='" << y << "' r='3.6' fill='" << s.color << "'/>\n";
            }
    }
    o << "</svg>\n";
    // legend
    int lx = c.legendPos == "left" ? L + 14 : L + pw - 14, ly = T + 14;
    int maxw = maxNameLen;
    int boxw = 36 + maxw * 7, boxh = 8 + 20 * (int)c.series.size();
    int bx = c.legendPos == "left" ? lx : lx - boxw;
    if (outside) {
        bx = L + pw + 12;
        ly = T;
    }
    o << "<rect x='" << bx << "' y='" << ly << "' width='" << boxw << "' height='" << boxh
      << "' fill='white' fill-opacity='0.92' stroke='#cbd2d9' rx='4'/>\n";
    for (size_t i = 0; i < c.series.size(); i++) {
        auto &s = c.series[i];
        int yy = ly + 18 + 20 * (int)i;
        o << "<line x1='" << bx + 8 << "' x2='" << bx + 26 << "' y1='" << yy - 4 << "' y2='" << yy - 4 << "' stroke='"
          << s.color << "' stroke-width='3'" << (s.dashed ? " stroke-dasharray='5,3'" : "") << "/>\n";
        o << "<text x='" << bx + 31 << "' y='" << yy << "' font-size='12' fill='#323f4b'>" << esc(s.name) << "</text>\n";
    }
    o << "</svg>\n";
    return o.str();
}

struct BarGroup {
    std::string label;
    std::vector<double> values;
};

inline std::string barChart(const std::string &title, const std::string &ylabel, const std::vector<std::string> &names,
                            const std::vector<std::string> &colors, const std::vector<BarGroup> &groups,
                            int width = 760, int height = 440) {
    double ymax = 0;
    for (auto &g : groups)
        for (double v : g.values) ymax = std::max(ymax, v);
    ymax *= 1.15;
    if (ymax <= 0) ymax = 1;
    const int L = 70, R = 20, T = 48, B = 70;
    int pw = width - L - R, ph = height - T - B;
    auto Y = [&](double y) { return T + ph - ph * y / ymax; };
    std::ostringstream o;
    o << header(width, height);
    o << "<text x='" << width / 2 << "' y='28' text-anchor='middle' font-size='17' font-weight='600' fill='#1f2933'>"
      << esc(title) << "</text>\n";
    double ys = niceStep(ymax, 6);
    for (double y = 0; y <= ymax + 1e-9; y += ys) {
        o << "<line x1='" << L << "' x2='" << L + pw << "' y1='" << Y(y) << "' y2='" << Y(y) << "' stroke='#e4e7eb'/>\n";
        o << "<text x='" << L - 8 << "' y='" << Y(y) + 4 << "' text-anchor='end' font-size='12' fill='#52606d'>" << num(y)
          << "</text>\n";
    }
    double gw = (double)pw / groups.size();
    int nb = (int)names.size();
    double bw = gw * 0.72 / nb;
    for (size_t g = 0; g < groups.size(); g++) {
        double x0 = L + g * gw + gw * 0.14;
        for (int b = 0; b < nb && b < (int)groups[g].values.size(); b++) {
            double v = groups[g].values[b];
            o << "<rect x='" << x0 + b * bw << "' y='" << Y(v) << "' width='" << bw - 2 << "' height='" << T + ph - Y(v)
              << "' fill='" << colors[b] << "'/>\n";
            o << "<text x='" << x0 + b * bw + bw / 2 - 1 << "' y='" << Y(v) - 4
              << "' text-anchor='middle' font-size='10.5' fill='#323f4b'>" << num(v) << "</text>\n";
        }
        o << "<text x='" << L + g * gw + gw / 2 << "' y='" << T + ph + 20
          << "' text-anchor='middle' font-size='12' fill='#323f4b'>" << esc(groups[g].label) << "</text>\n";
    }
    o << "<rect x='" << L << "' y='" << T << "' width='" << pw << "' height='" << ph << "' fill='none' stroke='#9aa5b1'/>\n";
    o << "<text transform='translate(20," << T + ph / 2 << ") rotate(-90)' text-anchor='middle' font-size='13' fill='#323f4b'>"
      << esc(ylabel) << "</text>\n";
    double lx = L;
    for (int b = 0; b < nb; b++) {
        o << "<rect x='" << lx << "' y='" << height - 26 << "' width='12' height='12' fill='" << colors[b] << "'/>\n";
        o << "<text x='" << lx + 17 << "' y='" << height - 16 << "' font-size='12' fill='#323f4b'>" << esc(names[b])
          << "</text>\n";
        lx += 30 + names[b].size() * 7.2;
    }
    o << "</svg>\n";
    return o.str();
}

}  // namespace plot
