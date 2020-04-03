#ifndef FGC_JSD_H__
#define FGC_JSD_H__
#include "fgc/distance.h"
#include "distmat/distmat.h"
#include "fgc/kmeans.h"
#include "fgc/coreset.h"
#include <boost/math/special_functions/digamma.hpp>
#include <boost/math/special_functions/polygamma.hpp>
#include <set>


namespace fgc {

namespace jsd {

using namespace blz;
using namespace blz::distance;


enum ProbDivType {
    L1,
    L2,
    SQRL2,
    JSM, // Multinomial Jensen-Shannon Metric
    JSD, // Multinomial Jensen-Shannon Divergence
    MKL, // Multinomial KL Divergence
    POISSON, // Poisson KL
    HELLINGER,
    BHATTACHARYYA_METRIC,
    BHATTACHARYYA_DISTANCE,
    TOTAL_VARIATION_DISTANCE,
    LLR,
    EMD,
    WEMD, // Weighted Earth-mover's distance
    REVERSE_MKL,
    REVERSE_POISSON,
    UWLLR, /* Unweighted Log-likelihood Ratio.
            * Specifically, this is the D_{JSD}^{\lambda}(x, y),
            * where \lambda = \frac{N_p}{N_p + N_q}
            *
            */
    OLLR,       // Old LLR, deprecated (included for compatibility/comparisons)
    ITAKURA_SAITO, // \sum_{i=1}^D[\frac{a_i}{b_i} - \log{\frac{a_i}{b_i}} - 1]
    REVERSE_ITAKURA_SAITO, // Reverse I-S
    WLLR = LLR, // Weighted Log-likelihood Ratio, now equivalent to the LLR
    TVD = TOTAL_VARIATION_DISTANCE,
    WASSERSTEIN=EMD,
    PSD = JSD, // Poisson JSD, but algebraically equivalent
    PSM = JSM,
    IS=ITAKURA_SAITO
};

namespace detail {
static constexpr INLINE bool  needs_logs(ProbDivType d)  {
    switch(d) {
        case JSM: case JSD: case MKL: case POISSON: case LLR: case OLLR: case ITAKURA_SAITO:
        case REVERSE_MKL: case REVERSE_POISSON: case UWLLR: case REVERSE_ITAKURA_SAITO: return true;
        default: break;
    }
    return false;
}


static constexpr INLINE bool  needs_sqrt(ProbDivType d) {
    return d == HELLINGER || d == BHATTACHARYYA_METRIC || d == BHATTACHARYYA_DISTANCE;
}

static constexpr INLINE bool is_symmetric(ProbDivType d) {
    switch(d) {
        case L1: case L2: case EMD: case HELLINGER: case BHATTACHARYYA_DISTANCE: case BHATTACHARYYA_METRIC:
        case JSD: case JSM: case LLR: case UWLLR: case SQRL2: case TOTAL_VARIATION_DISTANCE: case OLLR:
            return true;
        default: ;
    }
    return false;
}


static constexpr INLINE const char *prob2str(ProbDivType d) {
    switch(d) {
        case BHATTACHARYYA_DISTANCE: return "BHATTACHARYYA_DISTANCE";
        case BHATTACHARYYA_METRIC: return "BHATTACHARYYA_METRIC";
        case EMD: return "EMD";
        case HELLINGER: return "HELLINGER";
        case JSD: return "JSD/PSD";
        case JSM: return "JSM/PSM";
        case L1: return "L1";
        case L2: return "L2";
        case LLR: return "LLR";
        case OLLR: return "OLLR";
        case UWLLR: return "UWLLR";
        case ITAKURA_SAITO: return "ITAKURA_SAITO";
        case MKL: return "MKL";
        case POISSON: return "POISSON";
        case REVERSE_MKL: return "REVERSE_MKL";
        case REVERSE_POISSON: return "REVERSE_POISSON";
        case REVERSE_ITAKURA_SAITO: return "REVERSE_ITAKURA_SAITO";
        case SQRL2: return "SQRL2";
        case TOTAL_VARIATION_DISTANCE: return "TOTAL_VARIATION_DISTANCE";
        default: return "INVALID TYPE";
    }
}
static constexpr INLINE const char *prob2desc(ProbDivType d) {
    switch(d) {
        case BHATTACHARYYA_DISTANCE: return "Bhattacharyya distance: -log(dot(sqrt(x) * sqrt(y)))";
        case BHATTACHARYYA_METRIC: return "Bhattacharyya metric: sqrt(1 - BhattacharyyaSimilarity(x, y))";
        case EMD: return "Earth Mover's Distance: Optimal Transport";
        case HELLINGER: return "Hellinger Distance: sqrt(sum((sqrt(x) - sqrt(y))^2))/2";
        case JSD: return "Jensen-Shannon Divergence for Poisson and Multinomial models, for which they are equivalent";
        case JSM: return "Jensen-Shannon Metric, known as S2JSD and the Endres metric, for Poisson and Multinomial models, for which they are equivalent";
        case L1: return "L1 distance";
        case L2: return "L2 distance";
        case LLR: return "Log-likelihood Ratio under the multinomial model";
        case OLLR: return "Original log-likelihood ratio. This is likely not correct, but it is related to the Jensen-Shannon Divergence";
        case UWLLR: return "Unweighted Log-likelihood Ratio. This is effectively the Generalized Jensen-Shannon Divergence with lambda parameter corresponding to the fractional contribution of counts in the first observation. This is symmetric, unlike the G_JSD, because the parameter comes from the counts.";
        case MKL: return "Multinomial KL divergence";
        case POISSON: return "Poisson KL Divergence";
        case REVERSE_MKL: return "Reverse Multinomial KL divergence";
        case REVERSE_POISSON: return "Reverse KL divergence";
        case SQRL2: return "Squared L2 Norm";
        case TOTAL_VARIATION_DISTANCE: return "Total Variation Distance: 1/2 sum_{i in D}(|x_i - y_i|)";
        case ITAKURA_SAITO: return "Itakura-Saito divergence, a Bregman divergence [sum((a / b) - log(a / b) - 1 for a, b in zip(A, B))]";
        case REVERSE_ITAKURA_SAITO: return "Reversed Itakura-Saito divergence, a Bregman divergence";
        default: return "INVALID TYPE";
    }
}
static void print_measures() {
    std::set<ProbDivType> measures {
        L1,
        L2,
        SQRL2,
        JSM,
        JSD,
        MKL,
        POISSON,
        HELLINGER,
        BHATTACHARYYA_METRIC,
        BHATTACHARYYA_DISTANCE,
        TOTAL_VARIATION_DISTANCE,
        LLR,
        OLLR,
        EMD,
        REVERSE_MKL,
        REVERSE_POISSON,
        UWLLR,
        TOTAL_VARIATION_DISTANCE,
        WASSERSTEIN,
        PSD,
        PSM,
        ITAKURA_SAITO
    };
    for(const auto measure: measures) {
        std::fprintf(stderr, "Code: %d. Description: '%s'. Short name: '%s'\n", measure, prob2desc(measure), prob2str(measure));
    }
}
} // detail

template<typename MatrixType>
class ProbDivApplicator {
    //using opposite_type = typename base_type::OppositeType;
    MatrixType &data_;
    using VecT = blaze::DynamicVector<typename MatrixType::ElementType>;
    VecT row_sums_;
    std::unique_ptr<MatrixType> logdata_;
    std::unique_ptr<MatrixType> sqrdata_;
    std::unique_ptr<VecT> jsd_cache_;
    typename MatrixType::ElementType lambda_ = 0.5;
public:
    using FT = typename MatrixType::ElementType;
    using MT = MatrixType;
    using This = ProbDivApplicator<MatrixType>;
    using ConstThis = const ProbDivApplicator<MatrixType>;

    const ProbDivType measure_;
    const MatrixType &data() const {return data_;}
    size_t size() const {return data_.rows();}
    template<typename PriorContainer=blaze::DynamicVector<FT, blaze::rowVector>>
    ProbDivApplicator(MatrixType &ref,
                      ProbDivType measure=JSM,
                      Prior prior=NONE,
                      const PriorContainer *c=nullptr):
        data_(ref), logdata_(nullptr), measure_(measure)
    {
        prep(prior, c);
    }
    /*
     * Sets distance matrix, under measure_ (if not provided)
     * or measure (if provided as an argument).
     */
    template<typename MatType>
    void set_distance_matrix(MatType &m, bool symmetrize=false) const {set_distance_matrix(m, measure_, symmetrize);}

    template<typename MatType, ProbDivType measure>
    void set_distance_matrix(MatType &m, bool symmetrize=false) const {
        using blaze::sqrt;
        const size_t nr = m.rows();
        assert(nr == m.columns());
        assert(nr == data_.rows());
        static constexpr ProbDivType actual_measure = measure == JSM ? JSD: measure;
        for(size_t i = 0; i < nr; ++i) {
            CONST_IF((blaze::IsDenseMatrix_v<MatrixType>)) {
                for(size_t j = i + 1; j < nr; ++j) {
                    auto v = this->call<actual_measure>(i, j);
                    m(i, j) = v;
                }
            } else {
                OMP_PFOR
                for(size_t j = i + 1; j < nr; ++j) {
                    auto v = this->call<actual_measure>(i, j);
                    m(i, j) = v;
                }
            }
        }
        CONST_IF(measure == JSM) {
            CONST_IF(blaze::IsDenseMatrix_v<MatType> || blaze::IsSparseMatrix_v<MatType>) {
                m = blz::sqrt(m);
            } else CONST_IF(dm::is_distance_matrix_v<MatType>) {
                blaze::CustomVector<FT, blaze::unaligned, blaze::unpadded> cv(const_cast<FT *>(m.data()), m.size());
                cv = blz::sqrt(cv);
            } else {
                std::transform(m.begin(), m.end(), m.begin(), [](auto x) {return std::sqrt(x);});
            }
        }
        CONST_IF(detail::is_symmetric(measure)) {
            //std::fprintf(stderr, "Symmetric measure %s/%s\n", detail::prob2str(measure), detail::prob2desc(measure));
            if(symmetrize) {
                fill_symmetric_upper_triangular(m);
            }
        } else {
            CONST_IF(dm::is_distance_matrix_v<MatType>) {
                std::fprintf(stderr, "Warning: using asymmetric measure with an upper triangular matrix. You are computing only half the values");
            } else {
                //std::fprintf(stderr, "Asymmetric measure %s/%s\n", detail::prob2str(measure), detail::prob2desc(measure));
                for(size_t i = 1; i < nr; ++i) {
                    CONST_IF((blaze::IsDenseMatrix_v<MatrixType>)) {
                        //std::fprintf(stderr, "Filling bottom half\n");
                        for(size_t j = 0; j < i; ++j) {
                            auto v = this->call<measure>(i, j);
                            m(i, j) = v;
                        }
                    } else {
                        OMP_PFOR
                        for(size_t j = 0; j < i; ++j) {
                            auto v = this->call<measure>(i, j);
                            m(i, j) = v;
                        }
                    }
                    m(i, i) = 0.;
                }
            }
        }
    }
    template<typename MatType>
    void set_distance_matrix(MatType &m, ProbDivType measure, bool symmetrize=false) const {
        switch(measure) {
            case TOTAL_VARIATION_DISTANCE: set_distance_matrix<MatType, TOTAL_VARIATION_DISTANCE>(m, symmetrize); break;
            case L1:                       set_distance_matrix<MatType, L1>(m, symmetrize); break;
            case L2:                       set_distance_matrix<MatType, L2>(m, symmetrize); break;
            case SQRL2:                    set_distance_matrix<MatType, SQRL2>(m, symmetrize); break;
            case JSD:                      set_distance_matrix<MatType, JSD>(m, symmetrize); break;
            case JSM:                      set_distance_matrix<MatType, JSM>(m, symmetrize); break;
            case REVERSE_MKL:              set_distance_matrix<MatType, REVERSE_MKL>(m, symmetrize); break;
            case MKL:                      set_distance_matrix<MatType, MKL>(m, symmetrize); break;
            case EMD:                      set_distance_matrix<MatType, EMD>(m, symmetrize); break;
            case WEMD:                     set_distance_matrix<MatType, WEMD>(m, symmetrize); break;
            case REVERSE_POISSON:          set_distance_matrix<MatType, REVERSE_POISSON>(m, symmetrize); break;
            case POISSON:                  set_distance_matrix<MatType, POISSON>(m, symmetrize); break;
            case HELLINGER:                set_distance_matrix<MatType, HELLINGER>(m, symmetrize); break;
            case BHATTACHARYYA_METRIC:     set_distance_matrix<MatType, BHATTACHARYYA_METRIC>(m, symmetrize); break;
            case BHATTACHARYYA_DISTANCE:   set_distance_matrix<MatType, BHATTACHARYYA_DISTANCE>(m, symmetrize); break;
            case LLR:                      set_distance_matrix<MatType, LLR>(m, symmetrize); break;
            case UWLLR:                    set_distance_matrix<MatType, UWLLR>(m, symmetrize); break;
            case OLLR:                     set_distance_matrix<MatType, OLLR>(m, symmetrize); break;
            case ITAKURA_SAITO:            set_distance_matrix<MatType, ITAKURA_SAITO>(m, symmetrize); break;
            case REVERSE_ITAKURA_SAITO:    set_distance_matrix<MatType, REVERSE_ITAKURA_SAITO>(m, symmetrize); break;
            default: throw std::invalid_argument(std::string("unknown dissimilarity measure: ") + std::to_string(int(measure)) + prob2str(measure));
        }
    }
    template<typename OFT=FT>
    blaze::DynamicMatrix<OFT> make_distance_matrix(bool symmetrize=false) const {
        return make_distance_matrix<OFT>(measure_, symmetrize);
    }
    template<typename OFT=FT>
    blaze::DynamicMatrix<OFT> make_distance_matrix(ProbDivType measure, bool symmetrize=false) const {
        blaze::DynamicMatrix<OFT> ret(data_.rows(), data_.rows());
        set_distance_matrix(ret, measure, symmetrize);
        return ret;
    }

    // Accessors
    decltype(auto) weighted_row(size_t ind) const {
        return blz::row(data_, ind BLAZE_CHECK_DEBUG) * row_sums_[ind];
    }
    auto row(size_t ind) const {return blz::row(data_, ind BLAZE_CHECK_DEBUG);}
    auto logrow(size_t ind) const {return blz::row(*logdata_, ind BLAZE_CHECK_DEBUG);}
    auto sqrtrow(size_t ind) const {return blz::row(*sqrdata_, ind BLAZE_CHECK_DEBUG);}


    /*
     * Distances
     */
    INLINE auto operator()(size_t i, size_t j) const {
        return this->operator()(i, j, measure_);
    }
    template<ProbDivType constexpr_measure>
    INLINE FT call(size_t i, size_t j) const {
        FT ret;
        CONST_IF(constexpr_measure == TOTAL_VARIATION_DISTANCE) {
            ret = discrete_total_variation_distance(row(i), row(j));
        } else CONST_IF(constexpr_measure == L1) {
            ret = l1Norm(weighted_row(i) - weighted_row(j));
        } else CONST_IF(constexpr_measure == L2) {
            ret = l2Norm(weighted_row(i) - weighted_row(j));
        } else CONST_IF(constexpr_measure == SQRL2) {
            ret = blaze::sqrNorm(weighted_row(i) - weighted_row(j));
        } else CONST_IF(constexpr_measure == JSD) {
            ret = jsd(i, j);
        } else CONST_IF(constexpr_measure == JSM) {
            ret = jsm(i, j);
        } else CONST_IF(constexpr_measure == REVERSE_MKL) {
            ret = mkl(j, i);
        } else CONST_IF(constexpr_measure == MKL) {
            ret = mkl(i, j);
        } else CONST_IF(constexpr_measure == EMD) {
            ret = p_wasserstein(row(i), row(j));
        } else CONST_IF(constexpr_measure == WEMD) {
            ret = p_wasserstein(weighted_row(i), weighted_row(j));
        } else CONST_IF(constexpr_measure == REVERSE_POISSON) {
            ret = pkl(j, i);
        } else CONST_IF(constexpr_measure == POISSON) {
            ret = pkl(i, j);
        } else CONST_IF(constexpr_measure == HELLINGER) {
            ret = hellinger(i, j);
        } else CONST_IF(constexpr_measure == BHATTACHARYYA_METRIC) {
            ret = bhattacharyya_metric(i, j);
        } else CONST_IF(constexpr_measure == BHATTACHARYYA_DISTANCE) {
            ret = bhattacharyya_distance(i, j);
        } else CONST_IF(constexpr_measure == LLR) {
            ret = llr(i, j);
        } else CONST_IF(constexpr_measure == UWLLR) {
            ret = uwllr(i, j);
        } else CONST_IF(constexpr_measure == OLLR) {
            ret = ollr(i, j);
        } else CONST_IF(constexpr_measure == ITAKURA_SAITO) {
            ret = itakura_saito(i, j);
        } else {
            throw std::runtime_error(std::string("Unknown measure: ") + std::to_string(int(constexpr_measure)));
        }
        return ret;
    }
    INLINE FT operator()(size_t i, size_t j, ProbDivType measure) const {
        if(unlikely(i >= data_.rows() || j >= data_.rows())) {
            std::cerr << (std::string("Invalid rows selection: ") + std::to_string(i) + ", " + std::to_string(j) + '\n');
            std::exit(1);
        }
        FT ret;
        switch(measure) {
            case TOTAL_VARIATION_DISTANCE: ret = call<TOTAL_VARIATION_DISTANCE>(i, j); break;
            case L1: ret = call<L1>(i, j); break;
            case L2: ret = call<L2>(i, j); break;
            case SQRL2: ret = call<SQRL2>(i, j); break;
            case JSD: ret = call<JSD>(i, j); break;
            case JSM: ret = call<JSM>(i, j); break;
            case REVERSE_MKL: ret = call<REVERSE_MKL>(i, j); break;
            case MKL: ret = call<MKL>(i, j); break;
            case EMD: ret = call<EMD>(i, j); break;
            case WEMD: ret = call<WEMD>(i, j); break;
            case REVERSE_POISSON: ret = call<REVERSE_POISSON>(i, j); break;
            case POISSON: ret = call<POISSON>(i, j); break;
            case HELLINGER: ret = call<HELLINGER>(i, j); break;
            case BHATTACHARYYA_METRIC: ret = call<BHATTACHARYYA_METRIC>(i, j); break;
            case BHATTACHARYYA_DISTANCE: ret = call<BHATTACHARYYA_DISTANCE>(i, j); break;
            case LLR: ret = call<LLR>(i, j); break;
            case UWLLR: ret = call<UWLLR>(i, j); break;
            case OLLR: ret = call<OLLR>(i, j); break;
            case ITAKURA_SAITO: ret = call<ITAKURA_SAITO>(i, j); break;
            default: __builtin_unreachable();
        }
        return ret;
    }
    template<typename MatType>
    void operator()(MatType &mat, ProbDivType measure, bool symmetrize=false) {
        set_distance_matrix(mat, measure, symmetrize);
    }
    template<typename MatType>
    void operator()(MatType &mat, bool symmetrize=false) {
        set_distance_matrix(mat, symmetrize);
    }
    template<typename MatType>
    auto operator()() {
        return make_distance_matrix(measure_);
    }

    auto itakura_saito(size_t i, size_t j) const {
        FT ret;
        CONST_IF(IsSparseMatrix_v<MatrixType>) {
            ret = -std::numeric_limits<FT>::max();
            std::fprintf(stderr, "warning: Itakura-Saito cannot be computed to sparse vectors/matrices at %zu/%zu\n", i, j);
        } else {
            auto div = row(i) / row(j);
            ret = blaze::sum(div - blaze::log(div)) - row(i).size();
        }
        return ret;
    }

    auto hellinger(size_t i, size_t j) const {
        return sqrdata_ ? blaze::sqrNorm(sqrtrow(i) - sqrtrow(j))
                        : blaze::sqrNorm(blz::sqrt(row(i)) - blz::sqrt(row(j)));
    }
    auto jsd(size_t i, size_t j) const {
        assert(i < data_.rows());
        assert(j < data_.rows());
        FT ret;
        auto ri = row(i), rj = row(j);
        //constexpr FT logp5 = -0.693147180559945; // std::log(0.5)
        auto s = ri + rj;
        ret = jsd_cache_->operator[](i) + jsd_cache_->operator[](j) - blz::dot(s, blaze::neginf2zero(blaze::log(s * 0.5)));
#ifndef NDEBUG
        static constexpr typename MatrixType::ElementType threshold
            = std::is_same_v<typename MatrixType::ElementType, double>
                                ? 0.: -1e-5;
        assert(ret >= threshold || !std::fprintf(stderr, "ret: %g (numerical stability issues)\n", ret));
#endif
        return std::max(ret, static_cast<FT>(0.));
    }
    template<typename OT, typename=std::enable_if_t<!std::is_integral_v<OT>>, typename OT2>
    auto jsd(size_t i, const OT &o, const OT2 &olog) const {
        auto mnlog = evaluate(log(0.5 * (row(i) + o)));
        return (blz::dot(row(i), logrow(i) - mnlog) + blz::dot(o, olog - mnlog));
    }
    template<typename OT, typename=std::enable_if_t<!std::is_integral_v<OT>>>
    auto jsd(size_t i, const OT &o) const {
        auto olog = evaluate(blaze::neginf2zero(blz::log(o)));
        return jsd(i, o, olog);
    }
    auto mkl(size_t i, size_t j) const {
        // Multinomial KL
        return get_jsdcache(i) - blz::dot(row(i), logrow(j));
    }
    template<typename OT, typename=std::enable_if_t<!std::is_integral_v<OT>>>
    auto mkl(size_t i, const OT &o) const {
        // Multinomial KL
        return get_jsdcache(i) - blz::dot(row(i), blaze::neginf2zero(blz::log(o)));
    }
    template<typename OT, typename=std::enable_if_t<!std::is_integral_v<OT>>, typename OT2>
    auto mkl(size_t i, const OT &, const OT2 &olog) const {
        // Multinomial KL
        return blz::dot(row(i), logrow(i) - olog);
    }
    auto pkl(size_t i, size_t j) const {
        // Poission KL
        return get_jsdcache(i) - blz::dot(row(i), logrow(j)) + blz::sum(row(j) - row(i));
    }
    template<typename OT, typename=std::enable_if_t<!std::is_integral_v<OT>>, typename OT2>
    auto pkl(size_t i, const OT &o, const OT2 &olog) const {
        // Poission KL
        return get_jsdcache(i) - blz::dot(row(i), olog) + blz::sum(row(i) - o);
    }
    template<typename OT, typename=std::enable_if_t<!std::is_integral_v<OT>>>
    auto pkl(size_t i, const OT &o) const {
        return pkl(i, o, neginf2zero(blz::log(o)));
    }
    auto psd(size_t i, size_t j) const {
        // Poission JSD
        auto mnlog = evaluate(log(.5 * (row(i) + row(j))));
        return (blz::dot(row(i), logrow(i) - mnlog) + blz::dot(row(j), logrow(j) - mnlog));
    }
    template<typename OT, typename=std::enable_if_t<!std::is_integral_v<OT>>, typename OT2>
    auto psd(size_t i, const OT &o, const OT2 &olog) const {
        // Poission JSD
        auto mnlog = evaluate(log(.5 * (row(i) + o)));
        return (blz::dot(row(i), logrow(i) - mnlog) + blz::dot(o, olog - mnlog));
    }
    template<typename OT, typename=std::enable_if_t<!std::is_integral_v<OT>>>
    auto psd(size_t i, const OT &o) const {
        return psd(i, o, neginf2zero(blz::log(o)));
    }
    auto bhattacharyya_sim(size_t i, size_t j) const {
        return sqrdata_ ? blz::dot(sqrtrow(i), sqrtrow(j))
                        : blz::sum(blz::sqrt(row(i) * row(j)));
    }
    template<typename OT, typename=std::enable_if_t<!std::is_integral_v<OT>>, typename OT2>
    auto bhattacharyya_sim(size_t i, const OT &o, const OT2 &osqrt) const {
        return sqrdata_ ? blz::dot(sqrtrow(i), osqrt)
                        : blz::sum(blz::sqrt(row(i) * o));
    }
    template<typename OT, typename=std::enable_if_t<!std::is_integral_v<OT>>>
    auto bhattacharyya_sim(size_t i, const OT &o) const {
        return bhattacharyya_sim(i, o, blz::sqrt(o));
    }
    template<typename...Args>
    auto bhattacharyya_distance(Args &&...args) const {
        return -std::log(bhattacharyya_sim(std::forward<Args>(args)...));
    }
    template<typename...Args>
    auto bhattacharyya_metric(Args &&...args) const {
        return std::sqrt(1 - bhattacharyya_sim(std::forward<Args>(args)...));
    }
    template<typename...Args>
    auto psm(Args &&...args) const {return std::sqrt(std::forward<Args>(args)...);}
    auto llr(size_t i, size_t j) const {
            //blaze::dot(row(i), logrow(i)) * row_sums_[i]
            //+
            //blaze::dot(row(j), logrow(j)) * row_sums_[j]
            // X_j^Tlog(p_j)
            // X_k^Tlog(p_k)
            // (X_k + X_j)^Tlog(p_jk)
        const auto lhn = row_sums_[i], rhn = row_sums_[j];
        const auto lambda = lhn / (lhn + rhn), m1l = 1. - lambda;
        auto ret = lhn * get_jsdcache(i) + rhn * get_jsdcache(j)
            -
            blz::dot(weighted_row(i) + weighted_row(j),
                neginf2zero(blz::log(lambda * row(i) + m1l * row(j)))
            );
        assert(ret >= -1e-2 * (row_sums_[i] + row_sums_[j]) || !std::fprintf(stderr, "ret: %g\n", ret));
        return std::max(ret, 0.);
    }
    auto ollr(size_t i, size_t j) const {
        auto ret = get_jsdcache(i) * row_sums_[i] + get_jsdcache(j) * row_sums_[j]
            - blz::dot(weighted_row(i) + weighted_row(j), neginf2zero(blz::log((row(i) + row(j)) * .5)));
        return std::max(ret, 0.);
    }
    auto uwllr(size_t i, size_t j) const {
        const auto lhn = row_sums_[i], rhn = row_sums_[j];
        const auto lambda = lhn / (lhn + rhn), m1l = 1. - lambda;
        return
          std::max(
            lambda * get_jsdcache(i) +
                  m1l * get_jsdcache(j) -
               blz::dot(lambda * row(i) + m1l * row(j),
                        neginf2zero(blz::log(
                            lambda * row(i) + m1l * row(j)))),
          0.);
    }
    template<typename OT, typename=std::enable_if_t<!std::is_integral_v<OT>>>
    auto llr(size_t, const OT &) const {
        throw std::runtime_error("llr is not implemented for this.");
        return 0.;
    }
    template<typename OT, typename=std::enable_if_t<!std::is_integral_v<OT>>, typename OT2>
    auto llr(size_t, const OT &, const OT2 &) const {
        throw std::runtime_error("llr is not implemented for this.");
        return 0.;
    }
    template<typename...Args>
    auto jsm(Args &&...args) const {
        return std::sqrt(jsd(std::forward<Args>(args)...));
    }
    void set_lambda(FT param) {
        if(param < 0. || param > 1.)
            throw std::invalid_argument(std::string("Param for lambda ") + std::to_string(param) + " is out of range.");
        lambda_ = param;
    }
private:
    template<typename Container=blaze::DynamicVector<FT, blaze::rowVector>>
    void prep(Prior prior, const Container *c=nullptr) {
        switch(prior) {
            case NONE:
            break;
            case DIRICHLET:
                CONST_IF(!IsSparseMatrix_v<MatrixType>) {
                    data_ += static_cast<FT>(1);
                } else {
                    throw std::invalid_argument("Can't use Dirichlet prior for sparse matrix");
                }
                break;
            case GAMMA_BETA:
                if(c == nullptr) throw std::invalid_argument("Can't do gamma_beta with null pointer");
                CONST_IF(!IsSparseMatrix_v<MatrixType>) {
                    data_ += (*c)[0];
                } else {
                    throw std::invalid_argument("Can't use gamma beta prior for sparse matrix");
                }
            break;
            case FEATURE_SPECIFIC_PRIOR:
                if(c == nullptr) throw std::invalid_argument("Can't do feature-specific with null pointer");
                data_ += blz::expand(*c, data_.rows());
        }
        row_sums_.resize(data_.rows());
        {
            auto rowsumit = row_sums_.data();
            for(auto r: blz::rowiterator(data_)) {
                CONST_IF(blz::IsDenseMatrix_v<MatrixType>) {
                    if(prior == NONE) {
                        r += 1e-50;
                        assert(blz::min(r) > 0.);
                    }
                }
                const auto countsum = blz::sum(r);
                r /= countsum;
                *rowsumit++ = countsum;
            }
        }

        if(detail::needs_logs(measure_)) {
            logdata_.reset(new MatrixType(neginf2zero(log(data_))));
        }
        if(detail::needs_sqrt(measure_)) {
            sqrdata_.reset(new MatrixType(blz::sqrt(data_)));
        }
        if(logdata_) {
            jsd_cache_.reset(new VecT(data_.rows()));
            auto &jc = *jsd_cache_;
            for(size_t i = 0; i < jc.size(); ++i) {
                jc[i] = dot(row(i), logrow(i));
            }
        }
    }
    FT get_jsdcache(size_t index) const {
        assert(jsd_cache_ && jsd_cache_->size() > index);
        return (*jsd_cache_)[index];
    }
    FT get_llrcache(size_t index) const {
        assert(jsd_cache_ && jsd_cache_->size() > index);
        return get_jsdcache(index) * row_sums_->operator[](index);
        return (*jsd_cache_)[index] * row_sums_->operator[](index);
    }
}; // ProbDivApplicator

template<typename MT1, typename MT2>
struct PairProbDivApplicator {
    ProbDivApplicator<MT1> &pda_;
    ProbDivApplicator<MT2> &pdb_;
    PairProbDivApplicator(ProbDivApplicator<MT1> &lhs, ProbDivApplicator<MT2> &rhs): pda_(lhs), pdb_(rhs) {
        if(lhs.measure_ != rhs.measure_) throw std::runtime_error("measures must be the same (for preprocessing reasons).");
    }
    decltype(auto) operator()(size_t i, size_t j) const {
        return pda_(i, pdb_.row(j));
    }
};

template<typename MatrixType>
class MultinomialJSDApplicator: public ProbDivApplicator<MatrixType> {
    using super = ProbDivApplicator<MatrixType>;
    template<typename PriorContainer=blaze::DynamicVector<typename super::FT, blaze::rowVector>>
    MultinomialJSDApplicator(MatrixType &ref,
                             Prior prior=NONE,
                             const PriorContainer *c=nullptr):
        ProbDivApplicator<MatrixType>(ref, JSD, prior, c) {}
};
template<typename MatrixType>
class MultinomialLLRApplicator: public ProbDivApplicator<MatrixType> {
    using super = ProbDivApplicator<MatrixType>;
    template<typename PriorContainer=blaze::DynamicVector<typename super::FT, blaze::rowVector>>
    MultinomialLLRApplicator(MatrixType &ref,
                             Prior prior=NONE,
                             const PriorContainer *c=nullptr):
        ProbDivApplicator<MatrixType>(ref, LLR, prior, c) {}
};

template<typename MJD>
struct BaseOperand {
    using type = decltype(*std::declval<MJD>());
};

template<typename MatrixType, typename PriorContainer=blaze::DynamicVector<typename MatrixType::ElementType, blaze::rowVector>>
auto make_probdiv_applicator(MatrixType &data, ProbDivType type=JSM, Prior prior=NONE, const PriorContainer *pc=nullptr) {
#if VERBOSE_AF
    std::fprintf(stderr, "[%s:%s:%d] Making probdiv applicator with %d/%s as measure, %d/%s as prior, and %s for prior container.\n",
                 __PRETTY_FUNCTION__, __FILE__, __LINE__, int(type), detail::prob2str(type), int(prior), prior == NONE ? "No prior": prior == DIRICHLET ? "Dirichlet" : prior == GAMMA_BETA ? "Gamma/Beta": "Feature-specific prior",
                pc == nullptr ? "No prior container": (std::string("Container of size ") + std::to_string(pc->size())).data());
#endif
    return ProbDivApplicator<MatrixType>(data, type, prior, pc);
}
template<typename MatrixType, typename PriorContainer=blaze::DynamicVector<typename MatrixType::ElementType, blaze::rowVector>>
auto make_jsm_applicator(MatrixType &data, Prior prior=NONE, const PriorContainer *pc=nullptr) {
    return make_probdiv_applicator(data, JSM, prior, pc);
}


template<typename MatrixType>
auto make_kmc2(const ProbDivApplicator<MatrixType> &app, unsigned k, size_t m=2000, uint64_t seed=13) {
    wy::WyRand<uint64_t> gen(seed);
    return coresets::kmc2(app, gen, app.size(), k, m);
}

template<typename MatrixType>
auto make_kmeanspp(const ProbDivApplicator<MatrixType> &app, unsigned k, uint64_t seed=13) {
    wy::WyRand<uint64_t> gen(seed);
    return coresets::kmeanspp(app, gen, app.size(), k);
}

template<typename MatrixType, typename WFT=typename MatrixType::ElementType, typename IT=uint32_t>
auto make_d2_coreset_sampler(const ProbDivApplicator<MatrixType> &app, unsigned k, uint64_t seed=13, const WFT *weights=nullptr, coresets::SensitivityMethod sens=cs::LBK) {
    auto [centers, asn, costs] = make_kmeanspp(app, k, seed);
    coresets::CoresetSampler<typename MatrixType::ElementType, IT> cs;
    cs.make_sampler(app.size(), centers.size(), costs.data(), asn.data(), weights,
                    seed + 1, sens);
    return cs;
}

} // jsd
using jsd::ProbDivApplicator;
using jsd::make_d2_coreset_sampler;
using jsd::make_kmc2;
using jsd::make_kmeanspp;
using jsd::make_jsm_applicator;
using jsd::make_probdiv_applicator;



} // fgc

#endif
