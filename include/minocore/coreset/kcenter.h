#ifndef FGC_KCENTER_CORESET_H__
#define FGC_KCENTER_CORESET_H__
#include "minocore/optim/kcenter.h"

namespace minocore {
namespace coresets {
namespace outliers {

/*
// All algorithms in this namespace are from:
// Greedy Strategy Works for k-Center Clustering with Outliers and Coreset Construction
// Hu Ding, Haikuo Yu, Zixiu Wang
*/

template<typename IT=std::uint32_t, typename FT=float, typename Container=std::vector<std::pair<FT, IT>>,
         typename Cmp=std::greater<>>
struct fpq: public std::priority_queue<std::pair<double, IT>, Container, Cmp> {
    // priority queue providing access to underlying constainer with getc()
    // , a reserve function and that defaults to std::greater<> for farthest points.
    using super = std::priority_queue<std::pair<double, IT>, Container, Cmp>;
    using value_type = std::pair<double, IT>;

    IT size_;
    fpq(IT size=0): size_(size) {reserve(size);}
    fpq(const fpq &o) = default;
    void reserve(size_t n) {this->c.reserve(n);}
    auto &getc() {return this->c;}
    const auto &getc() const {return this->c;}
    void update(const fpq &o) {
        for(const auto v: o.getc())
            add(v);
    }
    void add(const value_type v) {
        if(this->size() < size_) this->push(v);
        else if(v > this->top()) {
            this->pop();
            this->push(v);
        }
    }
    void add(FT val, IT index) {
        if(this->size() < size_) {
            this->push(value_type(val, index));
        } else if(val > this->top().first) {
            this->pop();
            this->push(value_type(val, index));
        }
    }
};



template<typename IT>
struct bicriteria_result_t: public std::tuple<IVec<IT>, IVec<IT>, std::vector<std::pair<double, IT>>, double> {
    using super = std::tuple<IVec<IT>, IVec<IT>, std::vector<std::pair<double, IT>>, double>;
    template<typename...Args>
    bicriteria_result_t(Args &&...args): super(std::forward<Args>(args)...) {}
    auto &centers() {return std::get<0>(*this);}
    auto &assignments() {return std::get<1>(*this);}
    // alias
    auto &labels() {return assignments();}
    auto &outliers() {return std::get<2>(*this);}
    double outlier_threshold() const {return std::get<3>(*this);}
    size_t num_centers() const {return centers().size();}
};

/*
// Algorithm 1 from the above DYW paper
// Z = # outliers
// \mu = quality of coreset
// size of coreset: 2z + O((2/\mu)^p k)
// \gamma = z / n
*/

template<typename Iter, typename FT=shared::ContainedTypeFromIterator<Iter>,
         typename IT=std::uint32_t, typename RNG, typename Norm=sqrL2Norm>
bicriteria_result_t<IT>
kcenter_bicriteria(Iter first, Iter end, RNG &rng, size_t, double eps,
                   double gamma=0.001, size_t t = 100, double eta=0.01,
                   const Norm &norm=Norm())
{
    auto dm = make_index_dm(first, norm);
    // Step 1: constants
    assert(end > first);
    size_t np = end - first;
    const size_t z = std::ceil(gamma * np);
    std::fprintf(stderr, "z: %zu\n", z);
    size_t farthestchunksize = std::ceil((1 + eps) * z),
           samplechunksize = std::ceil(std::log(1./eta) / (1 - gamma));
    IVec<IT> ret;
    IVec<IT> labels(np);
    ret.reserve(samplechunksize);
    std::vector<FT> distances(np);
    // randomly select 'log(1/eta) / (1 - eps)' vertices from X and add them to E.
    while(ret.size() < samplechunksize) {
        // Assuming that this is relatively small and we can take bad asymptotic complexity
        auto newv = rng() % np;
        if(std::find(ret.begin(), ret.end(), newv) == ret.end())
            push_back(ret, newv);
    }
    assert(flat_hash_set<IT>(ret.begin(), ret.end()).size() == ret.size());
    if(samplechunksize > 100) {
        std::fprintf(stderr, "Warning: with samplechunksize %zu, it may end up taking a decent amount of time. Consider swapping this in for a hash set.", samplechunksize);
    }
    if(samplechunksize > farthestchunksize) {
        std::fprintf(stderr, "samplecc is %zu (> fcs %zu). changing gcs to scc + z (%zu)\n", samplechunksize, farthestchunksize, samplechunksize + z);
        farthestchunksize = samplechunksize + z;
    }
    fpq<IT, FT> pq(farthestchunksize);
    const auto fv = ret[0];
    labels[fv] = fv;
    distances[fv] = 0.;
    // Fill the priority queue from the first set
#ifdef _OPENMP
    #pragma omp declare reduction (merge : fpq<IT, FT> : omp_out.update(omp_in)) initializer(omp_priv(omp_orig))
    #pragma omp parallel for reduction(merge: pq)
#endif
    for(IT i = 0; i < np; ++i) {
        double dist = dm(fv, i);
        double newdist;
        IT label = 0; // This label is an index into the ret vector, rather than the actual index
        for(size_t j = 1, e = ret.size(); j < e; ++j) {
            if((newdist = dm(i, ret[j])) < dist) {
                label = j;
                dist = newdist;
            }
        }
        distances[i] = dist;
        labels[i] = ret[label];
        pq.add(dist, i);
    }
    IVec<IT> random_samples(samplechunksize);
    // modulo without a div/mod instruction, much faster
    schism::Schismatic<IT> div(farthestchunksize); // pq size
    assert(samplechunksize >= 1.);
    for(size_t j = 0;j < t;++j) {
        //std::fprintf(stderr, "j: %zu/%zu\n", j, t);
        // Sample 'samplechunksize' points from pq into random_samples.
        // Sample them
        size_t rsi = 0;
        IT *rsp = random_samples.data();
        do {
            IT index = div.mod(rng());
            // (Without replacement)
            if(std::find(rsp, rsp + rsi, index))
                rsp[rsi++] = index;
        } while(rsi < samplechunksize);
        // random_samples now contains indexes *into pq*
        assert(pq.getc().data());
        std::transform(rsp, rsp + rsi, rsp,
            [pqi=pq.getc().data()](auto x) {
            return pqi[x].second;
        });
        for(size_t i = 0; i < rsi; ++i)
            assert(rsp[i] < np);
        // random_samples now contains indexes *into original dataset*

        // Insert into solution
        for(auto it = rsp, e = rsp + rsi; it < e;++it) {
            if(std::find(ret.begin(), ret.end(), *it) != ret.end()) continue;
            distances[*it] = 0.;
            labels[*it] = *it;
            ret.pushBack(*it);
        }

        // compare each point against all of the new points
        pq.getc().clear(); // empty priority queue
        // Fill priority queue
#ifdef _OPENMP
    #pragma omp declare reduction (merge : fpq<IT, FT> : omp_out.update(omp_in)) initializer(omp_priv(omp_orig))
    #pragma omp parallel for reduction(merge: pq)
#endif
        for(size_t i = 0; i < np; ++i) {
            double dist = distances[i];
            if(dist == 0.) continue;
            double newdist;
            IT label = labels[i];
            for(size_t j = 0; j < rsi; ++j) {
                if((newdist = dm(i, rsp[j])) < dist)
                    dist = newdist, label = rsp[j];
            }
            distances[i] = dist;
            labels[i] = label;
            pq.add(dist, i);
        }
    }
    const double minmaxdist = pq.top().first;
    bicriteria_result_t<IT> bicret;
    assert(flat_hash_set<IT>(ret.begin(), ret.end()).size() == ret.size());
    bicret.centers() = std::move(ret);
    bicret.labels() = std::move(labels);
    bicret.outliers() = std::move(pq.getc());
    std::fprintf(stderr, "outliers size: %zu\n", bicret.outliers().size());
    std::get<3>(bicret) = minmaxdist;
    return bicret;
    // center ids, label assignments for all points besides outliers, outliers, and the distance of the closest excluded point
} // kcenter_bicriteria

/*
// Algorithm 2 from the above DYW paper
// Z = # outliers
// \gamma = z / n
*/

template<typename Iter, typename FT=shared::ContainedTypeFromIterator<Iter>,
         typename IT=std::uint32_t, typename RNG, typename Norm=L2Norm>
std::vector<IT>
kcenter_greedy_2approx_outliers(Iter first, Iter end, RNG &rng, size_t k, double eps,
                                double gamma=0.001,
                                const Norm &norm=Norm())
{
    auto dm = make_index_dm(first, norm);
    const size_t np = end - first;
    const size_t z = std::ceil(gamma * np);
    size_t farthestchunksize = std::ceil((1. + eps) * z);
    fpq<IT, FT> pq(farthestchunksize);
    //pq.reserve(farthestchunksize + 1);
    std::vector<IT> ret;
    std::vector<FT> distances(np, std::numeric_limits<FT>::max());
    ret.reserve(k);
    auto newc = rng() % np;
    ret.push_back(newc);
    do {
        //const auto &newel = first[newc];
        // Fill pq
#ifdef _OPENMP
    #pragma omp declare reduction (merge : fpq<IT, FT> : omp_out.update(omp_in)) initializer(omp_priv(omp_orig))
    #pragma omp parallel for reduction(merge: pq)
#endif
        for(IT i = 0; i < np; ++i) {
            double dist = distances[i];
            if(dist == 0.) continue;
            double newdist;
            if((newdist = dm(i, newc)) < dist)
                dist = newdist;
            distances[i] = dist;
            pq.add(dist, i);
        }

        // Sample point
        newc = pq.getc()[rng() % farthestchunksize].second;
        assert(newc < np);
        ret.push_back(newc);
        pq.getc().clear();
    } while(ret.size() < k);
    return ret;
}// kcenter_greedy_2approx_outliers

// Algorithm 3 (coreset construction)
template<typename Iter, typename FT=shared::ContainedTypeFromIterator<Iter>,
         typename IT=std::uint32_t, typename RNG, typename Norm=L2Norm>
coresets::IndexCoreset<IT, FT>
kcenter_coreset_outliers(Iter first, Iter end, RNG &rng, size_t k, double eps=0.1, double mu=.5,
                double rho=1.5,
                double gamma=0.001, double eta=0.01, const Norm &norm=Norm()) {
    // rho is 'D' for R^D (http://www.wisdom.weizmann.ac.il/~robi/teaching/2014b-SeminarGeometryAlgorithms/lecture1.pdf)
    // in Euclidean space, as worst-case, but usually better in real data with structure.
    assert(mu > 0. && mu <= 1.);
    const size_t np = end - first;
    size_t L = std::ceil(std::pow(2. / mu, rho) * k);
    size_t nrounds = std::ceil((L + std::sqrt(L)) / (1. - eta));
    auto bic = kcenter_bicriteria(first, end, rng, k, eps,
                                  gamma, nrounds, eta, norm);
    double rtilde = bic.outlier_threshold();
    std::fprintf(stderr, "outlier threshold: %f\n", rtilde);
    auto &centers = bic.centers();
    auto &labels = bic.labels();
    auto &outliers = bic.outliers();
#ifndef NDEBUG
    for(const auto c: centers)
        assert(c < np);
    for(const auto label: labels)
        assert(labels[label] == label);
#endif
    //std::vector<size_t> counts(centers.size());
    coresets::flat_hash_map<IT, uint32_t> counts;
    counts.reserve(centers.size());
    size_t i = 0;
    SK_UNROLL_8
    do ++counts[labels[i++]]; while(i < np);
    coresets::IndexCoreset<IT, FT> ret(centers.size() + outliers.size());
    std::fprintf(stderr, "ret size: %zu. centers size: %zu. counts size %zu. outliers size: %zu\n", ret.size(), centers.size(), counts.size(), outliers.size());
    for(i = 0; i < outliers.size(); ++i) {
        assert(outliers[i].second < np);
        ret.indices_[i] = outliers[i].second;
        ret.weights_[i] = 1.;
    }
    for(const auto &pair: counts) {
        assert(pair.first < np);
        ret.weights_[i] = pair.second;
        ret.indices_[i] = pair.first;
        ++i;
    }
    assert(i == ret.size());
    for(size_t i = 0; i < ret.indices_.size(); ++i) {
        assert(ret.indices_[i] < np);
    }
    return ret;
}
} // namespace outliers
using outliers::kcenter_coreset_outliers;
using outliers::kcenter_greedy_2approx_outliers;
} // namespace coresets
using coresets::outliers::kcenter_greedy_2approx_outliers;

} // namespace minocore

#endif /* FGC_KCENTER_CORESET_H__ */

