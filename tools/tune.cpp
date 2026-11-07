// Texel tuning: fit the evaluation weights to game results.
//
//   ./build/tune data/positions.txt [out.txt]
//
// Reads "FEN;result" lines (result from White's side: 1.0, 0.5, 0.0). The error is the mean
// squared difference between each result and sigmoid(K * eval / 400), the eval's implied
// winning chance. K is fitted once to the starting weights; then each weight is nudged up and
// down, keeping any change that lowers the error, with steps 8, 4, 2, 1. Prints the error as
// it goes and writes the final weights, in EvalParams order, as a C++ initializer list to
// out.txt (default tuned.txt).
#include "bitboard.h"
#include "eval.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

struct Sample {
    Position pos;
    double result;
};

static std::vector<Sample> samples;

static double sigmoid(double k, int score) { return 1.0 / (1.0 + std::pow(10.0, -k * score / 400.0)); }

// Mean squared error over all samples, split across threads.
static double error(double k) {
    int n_threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<double> sums(n_threads);
    std::vector<std::thread> pool;
    size_t chunk = (samples.size() + n_threads - 1) / n_threads;
    for (int t = 0; t < n_threads; t++) {
        pool.emplace_back([&, t] {
            double sum = 0;
            for (size_t i = t * chunk; i < std::min(samples.size(), (t + 1) * chunk); i++) {
                const Sample& s = samples[i];
                int white_score = s.pos.side == WHITE ? evaluate(s.pos) : -evaluate(s.pos);
                double d = s.result - sigmoid(k, white_score);
                sum += d * d;
            }
            sums[t] = sum;
        });
    }
    for (auto& th : pool) th.join();
    double total = 0;
    for (double s : sums) total += s;
    return total / samples.size();
}

// Weights that can't affect the eval: the king's piece value, pawns on the back ranks,
// pawn and king mobility, pawn and king attack units.
static bool fixed(int i) {
    EvalParams& P = EVAL;
    int* base = eval_params();
    auto in = [&](const int* a, int n) { return i >= a - base && i < a - base + n; };
    if (in(&P.piece_mg[KING], 1) || in(&P.piece_eg[KING], 1)) return true;
    for (int sq = 0; sq < 64; sq++) {
        bool back_rank = sq < 8 || sq >= 56;
        if (back_rank && (in(&P.pst_mg[PAWN][sq], 1) || in(&P.pst_eg[PAWN][sq], 1))) return true;
    }
    for (PieceType t : {PAWN, KING})
        if (in(&P.mobility_mg[t], 1) || in(&P.mobility_eg[t], 1) || in(&P.attack_units[t], 1)) return true;
    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: tune positions.txt [out.txt]\n";
        return 1;
    }
    init_bitboards();
    std::ifstream in(argv[1]);
    for (std::string line; std::getline(in, line);) {
        size_t semi = line.find(';');
        if (semi == std::string::npos) continue;
        Sample s;
        if (!s.pos.set_fen(line.substr(0, semi))) continue;
        s.result = std::stod(line.substr(semi + 1));
        samples.push_back(std::move(s));
    }
    std::cout << samples.size() << " positions, " << EVAL_PARAM_COUNT << " weights\n";

    double k = 1.0, best_k = 1.0, best_e = 1e9;
    for (k = 0.5; k <= 2.0; k += 0.05) {
        double e = error(k);
        if (e < best_e) best_e = e, best_k = k;
    }
    k = best_k;
    std::printf("K = %.2f, starting error %.6f\n", k, best_e);

    int* w = eval_params();
    for (int step : {8, 4, 2, 1}) {
        for (bool improved = true; improved;) {
            improved = false;
            for (int i = 0; i < EVAL_PARAM_COUNT; i++) {
                if (fixed(i)) continue;
                for (int dir : {+1, -1}) {
                    w[i] += dir * step;
                    double e = error(k);
                    if (e < best_e) {
                        best_e = e, improved = true;
                        break;
                    }
                    w[i] -= dir * step;
                }
            }
            std::printf("step %d: error %.6f\n", step, best_e);
            std::fflush(stdout);
        }
    }

    const char* out_path = argc > 2 ? argv[2] : "tuned.txt";
    std::ofstream out(out_path);
    // A C++ initializer list in EvalParams order, ready for src/eval_weights.inc.
    for (int i = 0; i < EVAL_PARAM_COUNT; i++) out << w[i] << ((i + 1) % 16 ? ", " : ",\n");
    out << '\n';
    std::printf("wrote %d weights to %s\n", EVAL_PARAM_COUNT, out_path);
}
