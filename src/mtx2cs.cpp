#include "minocore/minocore.h"
#include "blaze/util/Serialization.h"
#include "minocore/clustering/sqrl2.h"
#include "minocore/clustering/l2.h"
#include "minocore/clustering/l1.h"

using namespace minocore;

namespace dist = blz::distance;

using minocore::util::timediff2ms;

Opts opts;

void usage() {
    std::fprintf(stderr, "mtx2coreset <flags> [input file=""] [output_dir=mtx2coreset_output]\n"
                         "=== General/Formatting ===\n"
                         "-f: Use floats (instead of doubles)\n"
                         "-p: Set number of threads [1]\n"
                         "-x: Transpose matrix (to swap feature/instance labels) during loading.\n"
                         "-C: load csr format (4 files) rather than matrix.mtx\n\n\n"
                         "=== Dissimilarity Measures ===\n"
                         "-1: Use L1 Norm \n"
                         "-2: Use L2 Norm \n"
                         "-S: Use squared L2 Norm (k-means)\n"
                         "-M: Use multinomial KL divergence\n"
                         "-j: Use multinomial Jensen-Shannon divergence\n"
                         "-J: Use multinomial Jensen-Shannon metric (square root of JSD)\n"
                         "-P: Use probability squared L2 norm\n"
                         "-T: Use total variation distance\n\n\n"
                         "=== Prior settings ===\n"
                         "-d: Use Dirichlet prior. Default: no prior.\n"
                         "-g: Use the Gamma/Beta prior and set gamma's value [default: 1.]\n\n\n"
                         "=== Optimizer settings ===\n"
                         "-D: Use metric solvers before EM rather than D2 sampling\n\n\n"
                         "=== Coreset Construction ===\n"
                         "-c: Set coreset size [1000]\n"
                         "-k: k (number of clusters)\n"
                         "-K: Use KMC2 for D2 sampling rather than kmeans++. May be significantly faster, but may provide lower quality solution.\n\n\n"
                         "-h: Emit usage\n");
    std::exit(1);
}


template<typename FT>
int m2ccore(std::string in, std::string out, Opts opts)
{
    std::fprintf(stderr, "[%s] Starting main\n", __PRETTY_FUNCTION__);
    std::fprintf(stderr, "Parameters: %s\n", opts.to_string().data());
    auto tstart = std::chrono::high_resolution_clock::now();
    blz::SM<FT> sm;
    if(opts.load_csr) {
        std::fprintf(stderr, "Trying to load from csr\n");
        sm = csc2sparse<FT>(in);
    } else if(opts.load_blaze) {
        std::fprintf(stderr, "Trying to load from blaze\n");
        blaze::Archive<std::ifstream> arch(in);
        arch >> sm;
    } else {
        std::fprintf(stderr, "Trying to load from mtx\n");
        sm = mtx2sparse<FT>(in, opts.transpose_data);
    }
    std::tuple<std::vector<CType<FT>>, std::vector<uint32_t>, CType<FT>> hardresult;
    std::tuple<std::vector<CType<FT>>, blz::DM<FT>, CType<FT>> softresult;

    switch(opts.dis) {
        case dist::L1: case dist::TVD: {
            assert(min(sm) >= 0.);
            if(opts.dis == dist::TVD) for(auto r: blz::rowiterator(sm)) r /= blz::sum(r);
            if(opts.soft) {
                throw NotImplementedError("L1/TVD under soft clustering");
            } else {
                hardresult = l1_sum_core(sm, out, opts);
                std::fprintf(stderr, "Total cost: %g\n", blz::sum(std::get<2>(hardresult)));
            }
            break;
        }
        case dist::L2: case dist::PL2: {
            assert(min(sm) >= 0.);
            if(opts.dis == dist::PL2) for(auto r: blz::rowiterator(sm)) r /= blz::sum(r);
            if(opts.soft) {
                throw NotImplementedError("L2/PL2 under soft clustering");
            } else {
                hardresult = l2_sum_core(sm, out, opts);
                std::fprintf(stderr, "Total cost: %g\n", blz::sum(std::get<2>(hardresult)));
            }
            break;
        }
        case dist::SQRL2: case dist::PSL2: {
            if(opts.dis == dist::PSL2) for(auto r: blz::rowiterator(sm)) r /= blz::sum(r);
            if(opts.soft) {
                throw NotImplementedError("SQRL2/PSL2 under soft clustering");
            } else {
                hardresult = kmeans_sum_core(sm, out, opts);
            }
            break;
        }
        default: {
            std::fprintf(stderr, "%d/%s not supported\n", (int)opts.dis, blz::detail::prob2desc(opts.dis));
            throw NotImplementedError("Not yet");
        }
    }
    if(opts.soft) {
        throw 1;
    } else {
        auto &[centers, asn, costs] = hardresult;
        coresets::CoresetSampler<FT, uint32_t> cs;
        cs.make_sampler(sm.rows(), opts.k, costs.data(), asn.data(), nullptr, opts.seed, opts.sm);
        cs.write(out + ".coreset_sampler");
        std::FILE *ofp;
        if(!(ofp = std::fopen((out + ".centers").data(), "w"))) throw 1;
        std::fprintf(ofp, "#Center\tFeatures\t...\t...\n");
        for(size_t i = 0; i < opts.k; ++i) {
            std::fprintf(ofp, "%zu\t", i + 1);
            const auto &c(centers[i]);
            for(size_t j = 0; j < c.size() - 1; ++j)
                std::fprintf(ofp, "%0.12g\t", c[j]);
            std::fprintf(ofp, "%0.12g\n", c[opts.k - 1]);
        }
        std::fclose(ofp);
        if(!(ofp = std::fopen((out + ".assignments").data(), "w"))) throw 1;
        for(size_t i = 0; i < asn.size(); ++i) {
            std::fprintf(ofp, "%zu\t%u\n", i, asn[i]);
        }
        std::fclose(ofp);
        std::string fmt = sizeof(FT) == 4 ? ".float32": ".double";
        if(!(ofp = std::fopen((out + fmt + ".importance").data(), "w"))) throw 1;
        if(std::fwrite(cs.probs_.get(), sizeof(FT), cs.size(), ofp) != cs.size()) throw 2;
        std::fclose(ofp);
        if(!(ofp = std::fopen((out + fmt + ".costs").data(), "w"))) throw 1;
        if(std::fwrite(costs.data(), sizeof(FT), costs.size(), ofp) != costs.size()) throw 3;
        std::fclose(ofp);
    }
    auto tstop = std::chrono::high_resolution_clock::now();
    std::fprintf(stderr, "Full program took %gms\n", util::timediff2ms(tstart, tstop));
    return 0;
}

int main(int argc, char **argv) {
    std::string inpath, outpath;
    bool use_double = true;
    for(int c;(c = getopt(argc, argv, "s:c:k:g:p:K:BdjJxSMT12NCDfh?")) >= 0;) {
        switch(c) {
            case 'h': case '?': usage();          break;
            case 'B': opts.load_blaze = true; opts.load_csr = false; break;
            case 'f': use_double = false;         break;
            case 'c': opts.coreset_size = std::strtoull(optarg, nullptr, 10); break;
            case 'C': opts.load_csr = true;       break;
            case 'p': OMP_ONLY(omp_set_num_threads(std::atoi(optarg));)       break;
            case 'g': opts.gamma = std::atof(optarg); opts.prior = dist::GAMMA_BETA; break;
            case 'k': opts.k = std::atoi(optarg); break;
            case '1': opts.dis = dist::L1;        break;
            case '2': opts.dis = dist::L2;        break;
            case 'S': opts.dis = dist::SQRL2;     break;
            case 'T': opts.dis = dist::TVD;       break;
            case 'M': opts.dis = dist::MKL;       break;
            case 'j': opts.dis = dist::JSD;       break;
            case 'J': opts.dis = dist::JSM;       break;
            case 'P': opts.dis = dist::PSL2;      break;
            case 'K': opts.kmc2_rounds = std::strtoull(optarg, nullptr, 10); break;
            case 's': opts.seed = std::strtoull(optarg,0,10); break;
            case 'd': opts.prior = dist::DIRICHLET;    break;
            case 'D': opts.discrete_metric_search = true; break;
            case 'x': opts.transpose_data = true; break;
        }
    }
    if(argc == optind) usage();
    if(argc - 1 >= optind) {
        inpath = argv[optind];
        if(argc - 2 >= optind)
            outpath = argv[optind + 1];
    }
    if(outpath.empty()) {
        outpath = "mtx2coreset_output.";
        outpath += std::to_string(uint64_t(std::time(nullptr)));
    }
    return m2ccore<double>(inpath, outpath, opts);
#if 0
    return use_double ? m2ccore<double>(inpath, outpath, opts)
                      : m2ccore<float>(inpath, outpath, opts);
#endif
}