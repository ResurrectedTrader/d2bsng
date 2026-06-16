#pragma once

// Adapted from a reference A* pathfinding implementation.
// Changes from original:
//   - Replaced D2Structs.h / functions.hpp / spdlog includes with TestActMap.h
//   - Replaced std::shared_ptr<ActMap> with std::shared_ptr<TestActMap>
//   - Removed get_player_unit() / level-change check (replaced with no-op)
//   - Removed Vars.quitting check
//   - checkFlag references TestActMap collision flags via ActMap alias
//   - All A* algorithm logic, node management, and path reconstruction kept identical

#include "PathReducer.h"
#include "TestActMap.h"

#include <cassert>
#include <cmath>
#include <queue>
#include <vector>

namespace Mapping::Pathing {

inline int __fastcall Manhattan(const Point& start, const Point& end) {
    return 10 * (std::abs(start.x - end.x) + std::abs(start.y - end.y));
}

inline int __fastcall DiagonalShortcut(const Point& start, const Point& end) {
    const int xdist = std::abs(start.x - end.x);
    const int ydist = std::abs(start.y - end.y);
    return xdist > ydist ? 14 * ydist + 10 * (xdist - ydist) : 14 * xdist + 10 * (ydist - xdist);
}

inline int __fastcall Chebyshev(const Point& start, const Point& end) {
    const int xdist = start.x - end.x;
    const int ydist = start.y - end.y;
    return xdist > ydist ? xdist : ydist;
}

inline int __fastcall Euclidean(const Point& start, const Point& end) {
    double dx = end.x - start.x;
    double dy = end.y - start.y;
    dx = pow(dx, 2);
    dy = pow(dy, 2);
    return static_cast<int>(sqrt(dx + dy) * 10);
}

inline int __fastcall Slope(const Point& start, const Point& end) {
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    if (dx == 0.0)
        return 0;  // avoid div-by-zero; reference returns garbage
    return static_cast<int>(dy / dx);
}

inline bool __fastcall checkFlag(int flag) {
    return ((ActMap::BlockWalk | ActMap::BlockPlayer) & flag) > 0;
}

inline int __fastcall EstimateDistance(const Point& point, const Point& end) {
    return DiagonalShortcut(point, end);
}

class Node {
   public:
    Node* const parent;
    Point point;
    int g, h;

    Node() : parent(nullptr), g(0), h(0) {}

    Node(Point s, Node* p, int _g, int _h) : parent(p), point(s), g(_g), h(_h) {}
};

struct NodeComparer {
    bool __fastcall operator()(const Node* left, const Node* right) const {
        return left->g + left->h > right->g + right->h;
    }
};

template <class Allocator = std::allocator<Node>>
class AStarPath : public Pather {
    Allocator alloc;
    using traits_t = std::allocator_traits<decltype(alloc)>;

    std::shared_ptr<TestActMap> map;
    std::unique_ptr<Reducing::PathReducer> reducer;
    Estimator estimate;
    Distance distance;

    void ReverseList(Node* node, std::vector<Point>& list) {
        list.clear();
        const Node* current = node;
        do {
            list.insert(list.begin(), current->point);
        } while ((current = current->parent) != nullptr);
    }

    bool FindPath(const Point& start, const Point& end, Node** result, std::vector<Node*>& nodes) {
        std::priority_queue<Node*, std::vector<Node*>, NodeComparer> open;
        std::unordered_set<Point> closed;
        std::vector<Point> newNodes;
        Node* begin = alloc.allocate(1);

        // if we don't get a valid node, just return
        if (!begin)
            return false;

        traits_t::construct(alloc, begin, Node(start, nullptr, 0, estimate(start, end)));
        nodes.push_back(begin);
        open.push(begin);
        while (!open.empty()) {
            Node* current = open.top();
            open.pop();

            if (closed.contains(current->point)) [[unlikely]]
                continue;

            if (current->point == end) [[unlikely]] {
                *result = current;
                return true;
            }

            // Release lock -- no-op for TestActMap
            map->maybe_release_lock();

            // Removed: get_player_unit() / level-change check
            // Removed: Vars.quitting check

            const bool insert_result = closed.insert(current->point).second;
            assert(insert_result == true);
            (void)insert_result;

            newNodes.clear();
            reducer->GetOpenNodes(current->point, newNodes, end);
            while (!newNodes.empty()) {
                Point point = newNodes.back();
                newNodes.pop_back();

                if (point != end && reducer->Reject(point)) {
                    closed.insert(point);
                    continue;
                }
                Node* next = alloc.allocate(1);
                // if we don't get a valid node, just return
                if (!next)
                    return false;
                const int pointPenalty = reducer->GetPenalty(point);
                traits_t::construct(alloc, next,
                                    Node(point, current, current->g + distance(current->point, point) + pointPenalty,
                                         estimate(point, end)));
                nodes.push_back(next);
                open.push(next);
            }
        }
        return false;
    }

   public:
    AStarPath(const std::shared_ptr<TestActMap>& _map, std::unique_ptr<Reducing::PathReducer> _reducer,
              Estimator _estimate = EstimateDistance, Distance _distance = DiagonalShortcut)
        : alloc(Allocator()), map(_map), reducer(std::move(_reducer)), estimate(_estimate), distance(_distance) {}

    ~AStarPath() override = default;

    bool GetPath(const Point& _start, const Point& _end, std::vector<Point>& list) override {
        Node* result = nullptr;
        Point start = _start, end = _end;

        // if we don't have a valid start and end, try mutating the points
        if (reducer->Reject(start))
            reducer->MutatePoint(start);
        if (reducer->Reject(end))
            reducer->MutatePoint(end);

        std::vector<Node*> nodes;
        bool success = FindPath(start, end, &result, nodes);
        if (result) {
            std::vector<Point> in;
            ReverseList(result, in);
            reducer->Reduce(in, list);
        } else {
            list.clear();
        }

        for (auto& node : nodes) {
            traits_t::destroy(alloc, node);
            alloc.deallocate(node, 1);
        }

        return success;
    }
};

}  // namespace Mapping::Pathing
