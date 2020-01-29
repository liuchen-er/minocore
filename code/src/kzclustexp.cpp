#if defined(USE_BOOST_PARALLEL) && USE_BOOST_PARALLEL
#include "boost/graph/use_mpi.hpp"
#include "boost/graph/distributed/depth_first_search.hpp"
#include "fgc/relaxed_heap.hpp"
#endif
#include "fgc/graph.h"
#include "fgc/geo.h"
#include "fgc/parse.h"
#include "fgc/bicriteria.h"
#include "fgc/coreset.h"
#include "fgc/lsearch.h"
#include "fgc/jv.h"
#include "fgc/timer.h"
#include <ctime>
#include <getopt.h>
#include "blaze/util/Serialization.h"

template<typename T> class TD;


using namespace fgc;
using namespace boost;

static size_t str2nbytes(const char *s) {
    if(!s) return 0;
    size_t ret = std::strtoull(s, const_cast<char **>(&s), 10);
    switch(*s) {
        case 'G': case 'g': ret <<= 10; [[fallthrough]];
        case 'M': case 'm': ret <<= 10; [[fallthrough]];
        case 'K': case 'k': ret <<= 10;
    }
    return ret;
}



template<typename Graph, typename ICon, typename FCon, typename IT, typename RetCon, typename CSWT>
void calculate_distortion_centerset(Graph &x, const ICon &indices, FCon &costbuffer,
                             const std::vector<coresets::IndexCoreset<IT, CSWT>> &coresets,
                             RetCon &ret, double z)
{
    assert(ret.size() == coresets.size());
    const size_t nv = boost::num_vertices(x);
    const size_t ncs = coresets.size();
    {
        util::ScopedSyntheticVertex<Graph> vx(x);
        auto synthetic_vertex = vx.get();
        for(auto idx: indices)
            boost::add_edge(synthetic_vertex, idx, 0., x);
        boost::dijkstra_shortest_paths(x, synthetic_vertex, distance_map(&costbuffer[0]));
    }
    if(z != 1.) costbuffer = pow(costbuffer, z);
    double fullcost = 0.;
    OMP_PRAGMA("omp parallel for reduction(+:fullcost)")
    for(unsigned i = 0; i < nv; ++i) {
        fullcost += costbuffer[i];
    }
    const double fcinv = 1. / fullcost;
    OMP_PFOR
    for(size_t j = 0; j < ncs; ++j) {
        // In theory, this could be
        auto &cs = coresets[j];
        const auto indices = cs.indices_.data();
        const auto weights = cs.weights_.data();
        const size_t cssz = cs.size();
        double coreset_cost = 0.;
        SK_UNROLL_8
        for(unsigned i = 0; i < cssz; ++i) {
            coreset_cost += costbuffer[indices[i]] * weights[i];
        }
        ret[j] = std::abs(coreset_cost * fcinv - 1.);
    }
}

template<typename GraphT>
GraphT &
max_component(GraphT &g, std::vector<latlon_t> &coordinates) {
    auto ccomp = std::make_unique<uint32_t []>(boost::num_vertices(g));
    assert(&ccomp[0] == ccomp.get());
    unsigned ncomp = boost::connected_components(g, ccomp.get());
    if(ncomp != 1) {
        std::fprintf(stderr, "not connected. ncomp: %u\n", ncomp);
        std::vector<unsigned> counts(ncomp);
        const size_t nv = boost::num_vertices(g);
        OMP_PFOR
        for(size_t i = 0; i < nv; ++i) {
            auto ci = ccomp[i];
            OMP_ATOMIC
            ++counts[ci];
        }
        auto maxcomp = std::max_element(counts.begin(), counts.end()) - counts.begin();
        std::fprintf(stderr, "maxcmp %zu out of total %u\n", maxcomp, ncomp);
        flat_hash_map<uint64_t, uint64_t> remapper;
        size_t id = 0;
        SK_UNROLL_8
        for(size_t i = 0; i < nv; ++i) {
            if(ccomp[i] == maxcomp)
                remapper[i] = id++;
        }
        if(coordinates.size()) {
            std::vector<latlon_t> newcoordinates(counts[maxcomp]);
            for(const auto pair: remapper)
                newcoordinates[pair.second] = coordinates[pair.first];
            std::swap(newcoordinates, coordinates);
        }
        GraphT newg(counts[maxcomp]);
        typename boost::property_map <fgc::Graph<undirectedS>,
                             boost::edge_weight_t >::type EdgeWeightMap = get(boost::edge_weight, g);
        using MapIt = typename flat_hash_map<uint64_t, uint64_t>::const_iterator;
        MapIt lit, rit;
        for(const auto edge: g.edges()) {
            if((lit = remapper.find(source(edge, g))) != remapper.end() &&
               (rit = remapper.find(target(edge, g))) != remapper.end())
                boost::add_edge(lit->second, rit->second, EdgeWeightMap[edge], newg);
        }
#ifndef NDEBUG
        ncomp = boost::connected_components(newg, ccomp.get());
        assert(ncomp == 1);
#endif
        std::fprintf(stderr, "After reducing to largest connected component -- num edges: %zu. num nodes: %zu\n", newg.num_edges(), newg.num_vertices());
        std::swap(newg, g);
    }
    return g;
}

void print_header(std::ofstream &ofs, char **argv, unsigned nsamples, unsigned k, double z, size_t nv, size_t ne) {
    ofs << "##Command-line: '";
    while(*argv) {
        ofs << *argv;
        ++argv;
        if(*argv) ofs << ' ';
    }
    char buf[128];
    std::sprintf(buf, "'\n##z: %g\n##nsamples: %u\n##k: %u\n##nv: %zu\n##ne: %zu\n", z, nsamples, k, nv, ne);
    ofs << buf;
    ofs << "#coreset_size\tmax distortion (VX11)\tmean distortion (VX11)\t "
        << "max distortion (BFL16)\tmean distortion (BFL16)\t"
        << "max distortion (uniform sampling)\tmean distortion (uniform sampling)\t"
        << "mean distortion on approximate soln [VX11]\tmeandist on approx [BFL16]\tmean distortion on approximate solution, Uniform Sampling"
        << "\n";
}

void usage(const char *ex) {
    std::fprintf(stderr, "usage: %s <opts> [input file or ../data/dolphins.graph]\n"
                         "-k\tset k [12]\n"
                         "-z\tset z [1.]\n"
                         "-c\tAppend coreset size. Default: {100} (if empty)\n"
                         "-s\tPath to write coreset sampler to\n"
                         "-S\tSet maximum size of Thorup subsampled data. Default: infinity\n"
                         "-M\tSet maxmimum memory size to use. Default: 16GiB\n"
                         "-t\tSet number of sampled centers to test [500]\n"
                         "-T\tNumber of Thorup sampling trials [15]\n"
                         "-K\tAppend an 'extra' k to perform evaluations against. This must be smaller than the 'k' parameter.\n"
                         "  \tThe purpose of this is to demonstrate that a coreset for a k2 s.t. k2 > k1 is also a coreset for k1."
                         "-R\tSet random seed. Default: hash based on command-line arguments\n"
                         "-D\tUse full Thorup E algorithm (use the union of a number of Thorup D iterations for local search instead of the best-performing Thorup D sample).\n"
                         "-L\tLocal search for all potential centers -- use all vertices as potential sources, not just subsampled centers.\n"
                         "  \tThis has the potential for being more accurate than more focused searches, at the expense of both space and time\n"
                         "-r\tUse all potential destinations when generating approximate solution instead of only Thorup subsampled points\n"
                         "  \tThis has the potential for being more accurate than more focused searches, at the expense of both space and time\n"
                         "-b\tUse the best improvement at each iteration of local search instead of taking the first one found\n"
                , ex);
    std::exit(1);
}




void parse_coordinates(std::string fn, std::vector<latlon_t> &ret, BoundingBoxData bbd) {
    std::ifstream inh(fn);
    std::string line;
    size_t inout[2]{0, 0};
    while(line.empty() || line.front() != 'p')
        std::getline(inh, line);
    for(size_t offset;std::getline(inh, line) && (offset = line.find("->")) != line.npos;) {
        const char *s = line.data() + offset + 2;
        size_t index = std::strtoull(s, const_cast<char **>(&s), 10);
        double lat = std::strtod(++s, const_cast<char **>(&s));
        double lon = std::strtod(++s, const_cast<char **>(&s));
        ret.at(index - 1) = {lat, lon};
    }
    OMP_PFOR
    for(size_t i = 0; i < ret.size(); ++i) {
        OMP_ATOMIC
        ++inout[bbd.contains(ret[i])];
#if VERBOSE_AF
        if(i < 50u)
            std::cerr << ret[i].to_string() << '\n';
#endif
    }
    std::cerr << "in: " << inout[1] << ". out: " << inout[0] << '\n';
}

int main(int argc, char **argv) {
    unsigned k = 10;
    double z = 1.; // z = power of the distance norm
    std::string output_prefix;
    std::vector<unsigned> coreset_sizes;
    std::vector<unsigned> extra_ks;
    bool rectangular = false;
    bool use_thorup_d = true;
    unsigned testing_num_centersets = 500;
    size_t rammax = 16uLL << 30;
    bool best_improvement = false;
    bool local_search_all_vertices = false;
    unsigned coreset_testing_num_iters = 5;
    uint64_t seed = std::accumulate(argv, argv + argc, uint64_t(0),
        [](auto x, auto y) {
            return x ^ std::hash<std::string>{}(y);
        }
    );
    std::string fn = std::to_string(seed);
    std::string coreset_sampler_path;
    unsigned num_thorup_trials = 15;
    //bool test_samples_from_thorup_sampled = true;
    double eps = 0.1;
    BoundingBoxData bbox;
    for(int c;(c = getopt(argc, argv, "e:B:S:N:T:t:p:o:M:z:s:c:K:k:R:LbDrh?")) >= 0;) {
        switch(c) {
            case 'e': if((eps = std::atof(optarg)) > 1. || eps < 0.)
                        throw std::runtime_error("Required: 0 >= eps >= 1.");
                      break;
            case 'K': extra_ks.push_back(std::atoi(optarg)); break;
            case 'k': k = std::atoi(optarg); break;
            case 'z': z = std::atof(optarg); break;
            case 'L': local_search_all_vertices = true; break;
            case 'r': rectangular = true; break;
            case 'b': best_improvement = true; break;
            case 'R': seed = std::strtoull(optarg, nullptr, 10); break;
            case 'M': rammax = str2nbytes(optarg); break;
            case 'D': use_thorup_d = false; break;
            case 't': testing_num_centersets = std::atoi(optarg); break;
            case 'B': bbox = optarg; assert(bbox.set()); break;
            case 'N': coreset_testing_num_iters = std::atoi(optarg); break;
            case 'T': num_thorup_trials = std::atoi(optarg); break;
            case 'S': coreset_sampler_path = optarg; break;
            case 'p': OMP_SET_NT(std::atoi(optarg)); break;
            case 'o': output_prefix = optarg; break;
            case 'c': coreset_sizes.push_back(std::atoi(optarg)); break;
            case 'h': default: usage(argv[0]);
        }
    }
    for(auto ek: extra_ks) {
        if(ek > k) {
            throw std::runtime_error("extra ks must be less than k");
        }
    }
    if(coreset_sizes.empty()) {
        coreset_sizes = {
#if USE3
3, 6, 9, 18, 27, 54, 81, 162, 243, 486, 729, 1458, 2187, 4374, 6561, 13122, 19683
#else
5, 10, 15, 20, 25, 50, 75, 100, 125, 250, 375, 500, 625, 1250, 1875, 2500, 3125, 3750
#endif
// Auto-generated:
// from functools import reduce
// makez = lambda z, n: reduce(lambda x, y: x + y, ([y * x for x in range(1, z)] for y in map(lambda x: z**x, range(1, n))))
// makez(5, 9)
//10, 25, 50, 100, 150, 200, 250, 500, 750, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 10000
};
    }
    if(output_prefix.empty())
        output_prefix = std::to_string(seed);
    std::string input = argc == optind ? "../data/dolphins.graph": const_cast<const char *>(argv[optind]);
    std::srand(seed);
    std::fprintf(stderr, "Reading from file: %s\n", input.data());
    std::vector<latlon_t> coordinates;

    // Parse the graph
    util::Timer timer("parse time:");
    fgc::Graph<undirectedS, float> g = parse_by_fn(input);
    timer.stop();
    timer.display();
    using Vertex = typename boost::graph_traits<decltype(g)>::vertex_descriptor;
    //std::vector<Vertex> in_vertices, out_vertices;
    std::vector<Vertex> bbox_vertices;
    size_t nsampled_in = 0, nsampled_out = 0;
    if(bbox.set()) {
        assert(bbox.valid());
        if(input.find(".gr") != input.npos && input.find(".graph") == input.npos) {
            coordinates.resize(boost::num_vertices(g));
            parse_coordinates(input, coordinates, bbox);
        } else throw std::runtime_error("wrong format");
    }
#ifndef NDEBUG
    for(const auto vtx: bbox_vertices)
        assert(vtx < boost::num_vertices(g));
#endif
    std::fprintf(stderr, "nv: %zu. ne: %zu\n", boost::num_vertices(g), boost::num_edges(g));
    // Select only the component with the most edges.
    timer.restart("max component:");
    max_component(g, coordinates);
    timer.report();
    if(bbox.set()) {
        timer.restart("bbox sampling:");
        wy::WyRand<uint32_t, 2> bbox_rng(coordinates.size() + seed);
        std::vector<Vertex> out_vertices;
        std::uniform_real_distribution<float> urd;
        for(const auto vtx: g.vertices()) {
            assert(vtx < g.num_vertices());
            if(bbox.contains(coordinates.at(vtx))) {
                //in_vertices.push_back(vtx);
                if(urd(bbox_rng) < bbox.p_box) {
                    bbox_vertices.push_back(vtx);
                    ++nsampled_in;
                }
            } else {
                //out_vertices.push_back(vtx);
                if(urd(bbox_rng) < bbox.p_nobox) {
                    bbox_vertices.push_back(vtx);
                    ++nsampled_out;
                }
            }
        }
        timer.report();
        std::fprintf(stderr, "sampled in: %zu. sampled out: %zu. sample probs: %g, %g\n", nsampled_in, nsampled_out, bbox.p_box, bbox.p_nobox);
    }
    // Assert that it's connected, or else the problem has infinite cost.
    assert_connected(g);
    // Either the bbox is unset (include all vertices) or there's a bijection between coordinates
    // and vertices.
    assert(!bbox.set() || coordinates.size() == boost::num_vertices(g));

    std::vector<uint32_t> thorup_assignments;
    timer.restart("thorup sampling:");
    // nullptr here for the case of using all vertices
    // but nonzero if a bounding box has been used to select $X \subseteq V$.
    const std::vector<Vertex> *bbox_vertices_ptr = nullptr;
    if(bbox_vertices.size()) {
        bbox_vertices_ptr = &bbox_vertices;
#ifndef NDEBUG
        for(auto vtx: bbox_vertices)
            assert(vtx < boost::num_vertices(g));
        assert(bbox_vertices_ptr->size() == bbox_vertices.size());
        for(auto vtx: *bbox_vertices_ptr)
            assert(vtx < boost::num_vertices(g));
#endif
    }
    std::vector<Vertex> sampled;
    if(use_thorup_d) {
        assert_connected(g);
        std::tie(sampled, thorup_assignments) = thorup_sample_mincost(g, k, seed, num_thorup_trials, bbox_vertices_ptr);
    } else {
    // Use Thorup E, which performs D a number of times and returns the union thereof.
        sampled = thorup_sample(g, k, seed, /*max_sampled=*/0, bbox_vertices_ptr);
        // max_sampled = 0 means that the full set of nodes provided may be chosen
        auto [_, thorup_assignments] = get_costs(g, sampled);
        assert_connected(g);
    }
    timer.report();
    timer.restart("center counts:");
    std::vector<uint32_t> center_counts(sampled.size());
    OMP_PFOR
    for(size_t i = 0; i < thorup_assignments.size(); ++i) {
        OMP_ATOMIC
        ++center_counts[thorup_assignments[i]];
    }
    timer.report();
    std::fprintf(stderr, "[Phase 1] Thorup sampling complete. Sampled %zu points from input graph: %zu vertices, %zu edges.\n", sampled.size(), boost::num_vertices(g), boost::num_edges(g));

    std::unique_ptr<DiskMat<float>> diskmatptr;
    std::unique_ptr<blaze::DynamicMatrix<float>> rammatptr;
    const size_t ndatarows = rectangular ? boost::num_vertices(g): sampled.size();
    std::fprintf(stderr, "rect: %d. lsearch all vertices: %d. ndatarows: %zu\n", rectangular, local_search_all_vertices, ndatarows);

    timer.restart("distance matrix generation:");
    using CM = blaze::CustomMatrix<float, blaze::aligned, blaze::padded, blaze::rowMajor>;
    if(sampled.size() * ndatarows * sizeof(float) > rammax) {
        std::fprintf(stderr, "%zu * %zu * sizeof(float) > rammax %zu\n", sampled.size(), ndatarows, rammax);
        diskmatptr.reset(new DiskMat<float>(graph2diskmat(g, fn, &sampled, !rectangular, local_search_all_vertices)));
    } else {
        rammatptr.reset(new blaze::DynamicMatrix<float>(graph2rammat(g, fn, &sampled, !rectangular, local_search_all_vertices)));
    }
    timer.report();
    CM dm(diskmatptr ? diskmatptr->data(): rammatptr->data(), sampled.size(), ndatarows, diskmatptr ? diskmatptr->spacing(): rammatptr->spacing());
    if(z != 1.) {
        std::fprintf(stderr, "rescaling distances by the power of z\n");
        timer.restart("z rescaling");
        assert(z > 1.);
        dm = pow(abs(dm), z);
        timer.report();
    }
    if(!rectangular) {
        timer.restart("weighting columns:");
        for(size_t i = 0; i < center_counts.size(); ++i) {
            column(dm, i) *= center_counts[i];
        }
        timer.report();
    }
    std::fprintf(stderr, "[Phase 2] Distances gathered\n");

    // Perform Thorup sample before JV method.
    timer.restart("local search:");
    auto lsearcher = make_kmed_lsearcher(dm, k, eps, seed * seed + seed, best_improvement);
    lsearcher.run();
    timer.report();
    if(dm.rows() < 100 && k < 7) {
        fgc::util::Timer newtimer("exhaustive search");
        auto esearcher = make_kmed_esearcher(dm, k);
        esearcher.run();
    }
    auto med_solution = lsearcher.sol_;
    auto ccost = lsearcher.current_cost_;
    // Free memory
    if(diskmatptr) diskmatptr.reset();
    if(rammatptr) rammatptr.reset();

    std::fprintf(stderr, "[Phase 3] Local search completed. Cost for solution: %g\n", ccost);
    // Calculate the costs of this solution
    std::vector<uint32_t> approx_v(med_solution.begin(), med_solution.end());
    if(!local_search_all_vertices) {
        for(auto &i: approx_v) {
            // since these solutions are indices into the subsampled set
            i = sampled[i];
        }
    }
    // For locality when calculating
    shared::sort(approx_v.data(), approx_v.data() + approx_v.size());
    timer.restart("get costs:");
    auto [costs, assignments] = get_costs(g, approx_v);
    std::fprintf(stderr, "[Phase 4] Calculated costs and assignments for all points\n");
    if(z != 1.)
        costs = blaze::pow(costs, z);
    timer.report();
    // Build a coreset importance sampler based on it.
    coresets::CoresetSampler<float, uint32_t> sampler, bflsampler;
    timer.restart("make coreset samplers:");
    sampler.make_sampler(costs.size(), med_solution.size(), costs.data(), assignments.data(),
                         nullptr, (((seed * 1337) ^ (seed * seed * seed)) - ((seed >> 32) ^ (seed << 32))), coresets::VARADARAJAN_XIAO);
    bflsampler.make_sampler(costs.size(), med_solution.size(), costs.data(), assignments.data(),
                         nullptr, (((seed * 1337) + (seed * seed * seed)) ^ (seed >> 32) ^ (seed << 32)), coresets::BRAVERMAN_FELDMAN_LANG);
    if(!coreset_sampler_path.empty()) {
        sampler.write(coreset_sampler_path);
    }
    timer.report();
    assert(sampler.sampler_.get());
    assert(bflsampler.sampler_.get());
    seed = std::mt19937_64(seed)();
    wy::WyRand<uint32_t, 2> rng(seed);
    std::string ofname = output_prefix + ".table_out." + std::to_string(k) + ".tsv";
    std::ofstream tblout(ofname);
    print_header(tblout, argv, testing_num_centersets, k, z, boost::num_vertices(g), boost::num_edges(g));
    coresets::UniformSampler<float, uint32_t> uniform_sampler(costs.size());
    // We run the inner loop `coreset_testing_num_iters` times
    // and average the maximum distortion.
    // We do this because there are two sources of randomness:
    //     1. The randomly selected centers
    //     2. The randomly-generated coresets
    // By running this inner loop `coreset_testing_num_iters` times, we hope to better demonstrate
    // the expected behavior

    const size_t ncs = coreset_sizes.size();
    const size_t distvecsz = ncs * 3;
    blaze::DynamicVector<double> meanmaxdistortion(distvecsz, 0.),
                                 meanmeandistortion(distvecsz, 0.),
                                 sumfdistortion(distvecsz, 0.), tmpfdistortion(distvecsz); // distortions on F
    blaze::DynamicVector<double> fdistbuffer(boost::num_vertices(g)); // For F, for comparisons
    timer.restart("evaluate random centers " + std::to_string(coreset_testing_num_iters) + " times: ");
    for(size_t i = 0; i < coreset_testing_num_iters; ++i) {
        // The first ncs coresets are VX-sampled, the second ncs are BFL-sampled, and the last ncs
        // are uniformly randomly sampled.
        std::vector<coresets::IndexCoreset<uint32_t, float>> coresets;
        coresets.reserve(ncs * 3);
        for(auto coreset_size: coreset_sizes) {
            coresets.emplace_back(sampler.sample(coreset_size));
        }
        for(auto coreset_size: coreset_sizes) {
            coresets.emplace_back(bflsampler.sample(coreset_size));
        }
        for(auto coreset_size: coreset_sizes) {
            coresets.emplace_back(uniform_sampler.sample(coreset_size));
        }
        assert(coresets.size() == distvecsz);
        std::fprintf(stderr, "[Phase 5] Generated coresets for iter %zu/%u\n", i + 1, coreset_testing_num_iters);
        blaze::DynamicVector<double> maxdistortion(distvecsz, std::numeric_limits<double>::min()),
                                     meandistortion(distvecsz, 0.);
        OMP_PFOR
        for(size_t i = 0; i < testing_num_centersets; ++i) {
            //if(i % 10 == 0)
            //    std::fprintf(stderr, "Calculating distortion %zu/%zu\n", i, random_centers.rows());
            blaze::SmallArray<uint32_t, 16> random_centers;
            wy::WyRand<uint32_t, 2> rng(i + seed + coreset_testing_num_iters);
            while(random_centers.size() < k) {
                auto v = rng();
                if(std::find(random_centers.begin(), random_centers.end(), v) == random_centers.end())
                    random_centers.pushBack(v);
            }
            blaze::DynamicVector<double> distbuffer(boost::num_vertices(g));
            blaze::DynamicVector<double> currentdistortion(coresets.size());
            decltype(g) gcopy(g);
            calculate_distortion_centerset(gcopy, random_centers, distbuffer, coresets, currentdistortion, z);
            OMP_CRITICAL
            {
                maxdistortion = blaze::serial(max(maxdistortion, currentdistortion));
            }
            OMP_CRITICAL
            {
                meandistortion = blaze::serial(meandistortion + currentdistortion);
            }
        }
        calculate_distortion_centerset(g, approx_v, fdistbuffer, coresets, tmpfdistortion, z);
        sumfdistortion += tmpfdistortion;
        meanmaxdistortion += maxdistortion;
        meandistortion /= testing_num_centersets;
        meanmeandistortion += meandistortion;
        //std::cerr << "mean [" << i << "]\n" << meandistortion;
        //std::cerr << "max  [" <<  i << "]\n" << maxdistortion;
        //std::cerr << "Center distortion:" << (sumfdistortion /(i + 1)) << '\n';
    }
    timer.report();
    timer.reset();
    sumfdistortion /= coreset_testing_num_iters;
    meanmaxdistortion /= coreset_testing_num_iters;
    meanmeandistortion /= coreset_testing_num_iters;
    for(size_t i = 0; i < ncs; ++i) {
        tblout << coreset_sizes[i]
               << '\t' << meanmaxdistortion[i] << '\t' << meanmeandistortion[i]
               << '\t' << meanmaxdistortion[i + ncs] << '\t' << meanmeandistortion[i + ncs]
               << '\t' << meanmaxdistortion[i + ncs * 2] << '\t' << meanmeandistortion[i + ncs * 2]
               << '\t' << sumfdistortion[i]
               << '\t' << sumfdistortion[i + ncs]
               << '\t' << sumfdistortion[i + ncs * 2]
               << '\n';
    }
    for(auto ek: extra_ks) {
        std::string ofname_ok = output_prefix + ".table_out.ok." + std::to_string(ek) + ".tsv";
        std::ofstream ofs(ofname_ok);
        blaze::DynamicVector<double> maxdistortion(distvecsz, std::numeric_limits<double>::min()),
                                     meandistortion(distvecsz, 0.);
        std::vector<coresets::IndexCoreset<uint32_t, float>> coresets;
        coresets.reserve(ncs * 3);
        for(auto coreset_size: coreset_sizes) {
            coresets.emplace_back(sampler.sample(coreset_size));
        }
        for(auto coreset_size: coreset_sizes) {
            coresets.emplace_back(bflsampler.sample(coreset_size));
        }
        for(auto coreset_size: coreset_sizes) {
            coresets.emplace_back(uniform_sampler.sample(coreset_size));
        }
        assert(coresets.size() == distvecsz);
        OMP_PFOR
        for(size_t i = 0; i < testing_num_centersets; ++i) {
            blaze::SmallArray<uint32_t, 16> random_centers;
            wy::WyRand<uint32_t, 2> rng(i + seed + coreset_testing_num_iters);
            while(random_centers.size() < k) {
                auto v = rng();
                if(std::find(random_centers.begin(), random_centers.end(), v) == random_centers.end())
                    random_centers.pushBack(v);
            }
            blaze::DynamicVector<double> distbuffer(boost::num_vertices(g));
            blaze::DynamicVector<double> currentdistortion(coresets.size());
            decltype(g) gcopy(g);
            calculate_distortion_centerset(gcopy, random_centers, distbuffer, coresets, currentdistortion, z);
            OMP_CRITICAL
            {
                maxdistortion = blaze::serial(max(maxdistortion, currentdistortion));
            }
            OMP_CRITICAL
            {
                meandistortion = blaze::serial(meandistortion + currentdistortion);
            }
        }
        meandistortion /= testing_num_centersets;
        for(size_t i = 0; i < ncs; ++i) {
            ofs << coreset_sizes[i]
                << '\t' << maxdistortion[i] << '\t' << meandistortion[i]
                << '\t' << maxdistortion[i + ncs] << '\t' << meandistortion[i + ncs]
                << '\t' << maxdistortion[i + ncs * 2] << '\t' << meandistortion[i + ncs * 2]
                << '\n';
        }
    }
    return EXIT_SUCCESS;
}
