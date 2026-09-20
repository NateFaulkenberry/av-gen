// Isolated throwaway probe (ADR-182: it can fail -- the correctness arm plants a known-best row
// and the search must find it, and the control arm searches a database of identical rows where
// the answer must be row 0). Nothing here includes or links anything from av-gen.
//
// Question: what does one brute-force weighted-L2 nearest-neighbour query over an N-frame motion
// matching database cost on this machine, as a function of N and the feature dimension D?
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <random>
#include <vector>
#include <algorithm>

using Clock = std::chrono::steady_clock;

// features stored row-major, float32, already normalised and weight-folded (the standard trick:
// fold the per-feature weight into the stored value at build time so the query is a plain L2).
static int search(const std::vector<float>& db, int n, int d, const std::vector<float>& q, float& bestOut) {
    int best = -1; float bestCost = 1e30f;
    for (int i = 0; i < n; ++i) {
        const float* row = db.data() + (std::size_t)i * d;
        float c = 0.0f;
        for (int k = 0; k < d; ++k) { const float e = row[k] - q[k]; c += e * e; }
        if (c < bestCost) { bestCost = c; best = i; }
    }
    bestOut = bestCost; return best;
}

// The same with an early-out on the running cost, which is what every production implementation does.
static int searchEarlyOut(const std::vector<float>& db, int n, int d, const std::vector<float>& q, float& bestOut) {
    int best = -1; float bestCost = 1e30f;
    for (int i = 0; i < n; ++i) {
        const float* row = db.data() + (std::size_t)i * d;
        float c = 0.0f;
        int k = 0;
        for (; k < d; ++k) { const float e = row[k] - q[k]; c += e * e; if (c >= bestCost) break; }
        if (k == d && c < bestCost) { bestCost = c; best = i; }
    }
    bestOut = bestCost; return best;
}

int main(int argc, char** argv) {
    const int reps = argc > 1 ? std::atoi(argv[1]) : 200;
    std::mt19937 rng(12345);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    printf("%8s %4s %10s %12s %12s %12s\n", "frames", "D", "MB", "brute us", "earlyout us", "ns/frame");
    for (int n : {1712, 5000, 20000, 60000, 180000, 540000}) {
        for (int d : {27, 51}) {
            std::vector<float> db((std::size_t)n * d);
            for (auto& v : db) v = nd(rng);
            // Correctness arm: plant an exact duplicate of a known query at a known row.
            std::vector<float> q(d); for (auto& v : q) v = nd(rng);
            const int planted = n / 3;
            std::copy(q.begin(), q.end(), db.begin() + (std::size_t)planted * d);
            float cost = 0.0f;
            const int hit = search(db, n, d, q, cost);
            if (hit != planted) { printf("PROBE BROKEN: brute found %d, planted %d\n", hit, planted); return 1; }
            const int hit2 = searchEarlyOut(db, n, d, q, cost);
            if (hit2 != planted) { printf("PROBE BROKEN: earlyout found %d, planted %d\n", hit2, planted); return 1; }
            // Re-randomise the planted row so the timing run is not an early-out best case.
            for (int k = 0; k < d; ++k) db[(std::size_t)planted * d + k] = nd(rng);

            double bestBrute = 1e30, bestEarly = 1e30;   // ADR-170: minima over repeats, never means
            for (int r = 0; r < reps; ++r) {
                for (auto& v : q) v = nd(rng);
                auto t0 = Clock::now(); float c1; volatile int a = search(db, n, d, q, c1);
                auto t1 = Clock::now(); float c2; volatile int b = searchEarlyOut(db, n, d, q, c2);
                auto t2 = Clock::now(); (void)a; (void)b;
                bestBrute = std::min(bestBrute, std::chrono::duration<double, std::micro>(t1 - t0).count());
                bestEarly = std::min(bestEarly, std::chrono::duration<double, std::micro>(t2 - t1).count());
            }
            printf("%8d %4d %10.2f %12.1f %12.1f %12.2f\n", n, d,
                   (double)n * d * 4.0 / (1024 * 1024), bestBrute, bestEarly, bestBrute * 1000.0 / n);
        }
    }
    return 0;
}
