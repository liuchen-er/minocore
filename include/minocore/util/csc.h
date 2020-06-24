#ifndef CSC_H__
#define CSC_H__
#include "./shared.h"
#include "./timer.h"
#include "./blaze_adaptor.h"
#include "./io.h"
#include "./exception.h"
#include "mio/single_include/mio/mio.hpp"
#include <fstream>


namespace minocore {

namespace util {

static inline bool is_file(std::string path) noexcept {
    return ::access(path.data(), F_OK) != -1;
}

template<typename IndPtrType=uint64_t, typename IndicesType=uint64_t, typename DataType=uint32_t>
struct CSCMatrixView {
    using ElementType = DataType;
    const IndPtrType *const indptr_;
    const IndicesType *const indices_;
    const DataType *const data_;
    const uint64_t nnz_;
    const uint32_t nf_, n_;
    static_assert(std::is_integral_v<IndPtrType>, "IndPtr must be integral");
    static_assert(std::is_integral_v<IndicesType>, "Indices must be integral");
    static_assert(std::is_arithmetic_v<IndicesType>, "Data must be arithmetic");
    CSCMatrixView(const IndPtrType *indptr, const IndicesType *indices, const DataType *data,
                  uint64_t nnz, uint32_t nfeat, uint32_t nitems):
        indptr_(indptr),
        indices_(indices),
        data_(data),
        nnz_(nnz),
        nf_(nfeat), n_(nitems)
    {
    }
    struct CView: public std::pair<DataType *, size_t> {
        size_t index() const {return this->second;}
        DataType &value() {return *this->first;}
        const DataType &value() const {return *this->first;}
    };
    struct ConstCView: public std::pair<const DataType*, size_t> {
        size_t index() const {return this->second;}
        const DataType &value() const {return *this->first;}
    };
    struct Column {
        const CSCMatrixView &mat_;
        size_t start_;
        size_t stop_;

        Column(const CSCMatrixView &mat, size_t start, size_t stop): mat_(mat), start_(start), stop_(stop) {}
        size_t nnz() const {return stop_ - start_;}
        size_t size() const {return mat_.columns();}
        template<bool is_const>
        struct ColumnIteratorBase {
            using ViewType = std::conditional_t<is_const, ConstCView, CView>;
            using ColType = std::conditional_t<is_const, const Column, Column>;
            using ViewedType = std::conditional_t<is_const, const DataType, DataType>;
            using difference_type = std::ptrdiff_t;
            using value_type = ViewedType;
            using reference = ViewedType &;
            using pointer = ViewedType *;
            using iterator_category = std::random_access_iterator_tag;
            ColType &col_;
            size_t index_;
            private:
            mutable ViewType data_;
            public:

            template<bool oconst>
            bool operator==(const ColumnIteratorBase<oconst> &o) const {
                return index_ == o.index_;
            }
            template<bool oconst>
            bool operator!=(const ColumnIteratorBase<oconst> &o) const {
                return index_ != o.index_;
            }
            template<bool oconst>
            bool operator<(const ColumnIteratorBase<oconst> &o) const {
                return index_ < o.index_;
            }
            template<bool oconst>
            bool operator>(const ColumnIteratorBase<oconst> &o) const {
                return index_ > o.index_;
            }
            template<bool oconst>
            bool operator<=(const ColumnIteratorBase<oconst> &o) const {
                return index_ <= o.index_;
            }
            template<bool oconst>
            bool operator>=(const ColumnIteratorBase<oconst> &o) const {
                return index_ >= o.index_;
            }
            template<bool oconst>
            difference_type operator-(const ColumnIteratorBase<oconst> &o) const {
                return this->index_ - o.index_;
            }
            ColumnIteratorBase<is_const> &operator++() {
                ++index_;
                return *this;
            }
            ColumnIteratorBase<is_const> operator++(int) {
                ColumnIteratorBase ret(col_, index_);
                ++index_;
                return ret;
            }
            const CView &operator*() const {
                set();
                return data_;
            }
            CView &operator*() {
                set();
                return data_;
            }
            void set() const {
                data_.first = const_cast<DataType *>(&col_.mat_.data_[index_]);
                data_.second = col_.mat_.indices_[index_];
            }
            ViewType *operator->() {
                set();
                return &data_;
            }
            const ViewType *operator->() const {
                set();
                return &data_;
            }
            ColumnIteratorBase(ColType &col, size_t ind): col_(col), index_(ind) {
            }
        };
        using ColumnIterator = ColumnIteratorBase<false>;
        using ConstColumnIterator = ColumnIteratorBase<true>;
        ColumnIterator begin() {return ColumnIterator(*this, start_);}
        ColumnIterator end()   {return ColumnIterator(*this, stop_);}
        ConstColumnIterator begin() const {return ConstColumnIterator(*this, start_);}
        ConstColumnIterator end()   const {return ConstColumnIterator(*this, stop_);}
    };
    auto column(size_t i) const {
        return Column(*this, indptr_[i], indptr_[i + 1]);
    }
    size_t nnz() const {return nnz_;}
    size_t rows() const {return n_;}
    size_t columns() const {return nf_;}
};

template<typename T>
struct is_csc_view: public std::false_type {};
template<typename IndPtrType, typename IndicesType, typename DataType>
struct is_csc_view<CSCMatrixView<IndPtrType, IndicesType, DataType>>: public std::true_type {};

template<typename T>
static constexpr bool is_csc_view_v = is_csc_view<T>::value;


template<typename DataType, typename IndPtrType, typename IndicesType>
size_t nonZeros(const typename CSCMatrixView<IndPtrType, IndicesType, DataType>::Column &col) {
    return col.nnz();
}
template<typename DataType, typename IndPtrType, typename IndicesType>
size_t nonZeros(const CSCMatrixView<IndPtrType, IndicesType, DataType> &mat) {
    return mat.nnz();
}

template<typename FT=float, typename IndPtrType, typename IndicesType, typename DataType>
blz::SM<FT, blaze::rowMajor> csc2sparse(const CSCMatrixView<IndPtrType, IndicesType, DataType> &mat, bool skip_empty=false) {
    blz::SM<FT, blaze::rowMajor> ret(mat.n_, mat.nf_);
    ret.reserve(mat.nnz_);
    size_t used_rows = 0, i;
    for(i = 0; i < mat.n_; ++i) {
        auto col = mat.column(i);
        if(mat.n_ > 100000 && i % 10000 == 0) std::fprintf(stderr, "%zu/%u\r", i, mat.n_);
        if(skip_empty && 0u == col.nnz()) continue;
        for(auto s = col.start_; s < col.stop_; ++s) {
            ret.append(used_rows, mat.indices_[s], mat.data_[s]);
        }
        ret.finalize(used_rows++);
    }
    if(used_rows != i) std::fprintf(stderr, "Only used %zu/%zu rows, skipping empty rows\n", used_rows, i);
    std::fprintf(stderr, "Parsed matrix of %zu/%zu\n", ret.rows(), ret.columns());
    return ret;
}

template<typename FT=float, typename IndPtrType=uint64_t, typename IndicesType=uint64_t, typename DataType=uint32_t>
blz::SM<FT, blaze::rowMajor> csc2sparse(std::string prefix, bool skip_empty=false) {
    util::Timer t("csc2sparse load time");
    std::string indptrn  = prefix + "indptr.file";
    std::string indicesn = prefix + "indices.file";
    std::string datan    = prefix + "data.file";
    std::string shape    = prefix + "shape.file";
    if(!is_file(indptrn)) throw std::runtime_error(std::string("Missing indptr: ") + indptrn);
    if(!is_file(indicesn)) throw std::runtime_error(std::string("Missing indices: ") + indicesn);
    if(!is_file(datan)) throw std::runtime_error(std::string("Missing indptr: ") + datan);
    if(!is_file(shape)) throw std::runtime_error(std::string("Missing indices: ") + shape);
    std::FILE *ifp = std::fopen(shape.data(), "rb");
    uint32_t dims[2];
    if(std::fread(dims, sizeof(uint32_t), 2, ifp) != 2) throw std::runtime_error("Failed to read dims from file");
    uint32_t nfeat = dims[0], nsamples = dims[1];
    std::fprintf(stderr, "nfeat: %u. nsample: %u\n", nfeat, nsamples);
    std::fclose(ifp);
    using mmapper = mio::mmap_source;
    mmapper indptr(indptrn), indices(indicesn), data(datan);
    CSCMatrixView<IndPtrType, IndicesType, DataType>
        matview((const IndPtrType *)indptr.data(), (const IndicesType *)indices.data(),
                (const DataType *)data.data(), indices.size() / sizeof(IndicesType),
                 nfeat, nsamples);
    std::fprintf(stderr, "indptr size: %zu\n", indptr.size() / sizeof(IndPtrType));
    std::fprintf(stderr, "indices size: %zu\n", indices.size() / sizeof(IndicesType));
    std::fprintf(stderr, "data size: %zu\n", data.size() / sizeof(DataType));
#ifndef MADV_REMOVE
#  define MADV_FLAGS (MADV_DONTNEED | MADV_FREE)
#else
#  define MADV_FLAGS (MADV_DONTNEED | MADV_REMOVE)
#endif
    ::madvise((void *)indptr.data(), indptr.size(), MADV_FLAGS);
    ::madvise((void *)indices.data(), indices.size(), MADV_FLAGS);
    ::madvise((void *)data.data(), data.size(), MADV_FLAGS);
#undef MADV_FLAGS
    return csc2sparse<FT>(matview, skip_empty);
}


template<typename FT, typename IT=size_t, bool SO=blaze::rowMajor>
struct COOElement {
    IT x, y;
    FT z;
    bool operator<(const COOElement &o) const {
        if constexpr(SO == blaze::rowMajor) {
            return std::tie(x, y, z) < std::tie(o.x, o.y, o.z);
        } else {
            return std::tie(y, x, z) < std::tie(o.y, o.x, o.z);
        }
    }
};

#if 0
template<typename FT, typename IT=size_t, bool SO=blaze::rowMajor>
struct COORadixTraits {
    using VT = COOElement<FT, IT, SO>;
    static constexpr int nBytes = sizeof(VT);
    static int kth_byte(const VT &x, int k) {
        if constexpr(SO == blaze::rowMajor) {
            return 0xFF & (k < 8 ? (x.x >> (k * 8)): (x.y >> ((k - 8) * 8)) );
        } else {
            return 0xFF & (k < 8 ? (x.y >> (k * 8)): (x.x >> ((k - 8) * 8)) );
        }
    }
    static constexpr bool compare(const VT &x, const VT &y) {
        if constexpr(SO == blaze::rowMajor) 
            return std::tie(x.x, x.y, x.z) < std::tie(y.x, y.y, y.z);
        else
            return std::tie(x.y, x.x, x.z) < std::tie(y.y, y.x, y.z);
    }
};
#endif

template<typename FT=float, bool SO=blaze::rowMajor, typename IT=size_t>
blz::SM<FT, SO> mtx2sparse(std::string path, bool perform_transpose=false) {
#ifndef NDEBUG
    TimeStamper ts("Parse mtx metadata");
#define MNTSA(x) ts.add_event((x))
#else
#define MNTSA(x)
#endif
    std::string line;
    auto [ifsp, fp] = io::xopen(path);
    auto &ifs = *ifsp;
    do std::getline(ifs, line); while(line.front() == '%');
    char *s;
    size_t nr      = std::strtoull(line.data(), &s, 10),
           columns = std::strtoull(s, &s, 10),
           lines   = std::strtoull(s, nullptr, 10);
    MNTSA("Reserve space");
    blz::SM<FT> ret(nr, columns);
    ret.reserve(lines);
    std::unique_ptr<COOElement<FT, IT, SO>[]> items(new COOElement<FT, IT, SO>[lines]);
    MNTSA("Read lines");
    size_t i = 0;
    while(std::getline(ifs, line)) {
        if(unlikely(i >= lines)) {
            char buf[1024];
            std::sprintf(buf, "i (%zu) > nnz (%zu). malformatted mtxfile at %s?\n", i, lines, path.data());
            MN_THROW_RUNTIME(buf);
        }
        char *s;
        auto x = std::strtoull(line.data(), &s, 10) - 1;
        auto y = std::strtoull(s, &s, 10) - 1;
        items[i++] = {x, y, static_cast<FT>(std::atof(s))};
    }
    if(i != lines) {
        char buf[1024];
        std::sprintf(buf, "i (%zu) != expected nnz (%zu). malformatted mtxfile at %s?\n", i, lines, path.data());
        MN_THROW_RUNTIME(buf);
    }
    std::fprintf(stderr, "processed %zu lines\n", lines);
    MNTSA(std::string("Sort ") + std::to_string(lines) + "items");
    auto it = items.get(), e = items.get() + lines;
    shared::sort(it, e);
    MNTSA("Set final matrix");
    for(size_t ci = 0;;) {
        if(it != e) {
            static constexpr bool RM = SO == blaze::rowMajor;
            const auto xv = RM ? it->x: it->y;
            while(ci < xv) ret.finalize(ci++);
            auto nextit = std::find_if(it, e, [xv](auto x) {
                if constexpr(RM) return x.x != xv; else return x.y != xv;
            });
#ifdef VERBOSE_AF
            auto diff = nextit - it;
            std::fprintf(stderr, "x at %d has %zu entries\n", int(it->x), diff);
            for(std::ptrdiff_t i = 0; i < diff; ++i)
                std::fprintf(stderr, "item %zu has %zu for y and %g\n", i, (it + i)->y, (it + i)->z);
#endif
            ret.reserve(ci, nextit - it);
            for(;it != nextit; ++it) {
                if constexpr(RM) {
                    ret.append(it->x, it->y, it->z);
                } else {
                    ret.append(it->x, it->y, it->z);
                }
            }
        } else {
            for(const size_t end = SO == blaze::rowMajor ? nr: columns;ci < end; ret.finalize(ci++));
            break;
        }
    }
    if(perform_transpose) {
        MNTSA("Perform transpose");
        blz::transpose(ret);
    }
    return ret;
}
#undef MNTSA


template<typename MT, bool SO>
std::pair<std::vector<size_t>, std::vector<size_t>>
erase_empty(blaze::Matrix<MT, SO> &mat) {
    std::pair<std::vector<size_t>, std::vector<size_t>> ret;
    std::fprintf(stderr, "Before resizing, %zu/%zu\n", (~mat).rows(), (~mat).columns());
    size_t orn = (~mat).rows(), ocn = (~mat).columns();
    auto rs = blaze::evaluate(blaze::sum<blaze::rowwise>(~mat));
    auto rsn = blz::functional::indices_if([&rs](auto x) {return rs[x] > 0.;}, rs.size());
    ret.first.assign(rsn.begin(), rsn.end());
    std::fprintf(stderr, "Eliminating %zu empty rows\n", orn - rsn.size());
    ~mat = rows(~mat, rsn);
    auto cs = blaze::evaluate(blaze::sum<blaze::columnwise>(~mat));
    auto csn = blz::functional::indices_if([&cs](auto x) {return cs[x] > 0.;}, cs.size());
    ret.second.assign(csn.begin(), csn.end());
    ~mat = columns(~mat, csn);
    std::fprintf(stderr, "Eliminating %zu empty columns\n", ocn - rsn.size());
    return ret;
}

template<typename FT=float, bool SO=blaze::rowMajor, typename IndPtrType, typename IndicesType, typename DataType>
blz::SM<FT, SO> csc2sparse(const IndPtrType *indptr, const IndicesType *indices, const DataType *data, 
                           size_t nnz, size_t nfeat, uint32_t nitems) {
    return csc2sparse<FT, SO>(CSCMatrixView<IndPtrType, IndicesType, DataType>(indptr, indices, data, nnz, nfeat, nitems));
}

} // namespace util
using util::csc2sparse;
using util::mtx2sparse;
using util::nonZeros;
using util::CSCMatrixView;
using util::is_csc_view;
using util::is_csc_view_v;

} // namespace minocore

#endif /* CSC_H__ */
