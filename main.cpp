// Implementation of the technique described in the blog post "Optimizing binary search"
// by David Geier (visit http://geidav.wordpress.com)

#include <functional>
#include <algorithm>
#include <iostream>
#include <cassert>
#include <random>
#include <vector>
#include <chrono>

// mapping function: specialized for 32-bit signed/unsigned
// integers and 32-bit floating point values.
// the mapping functions are used to create the LUT, because signed
// integers and floats are not comparable using bit-wise comparison.

template<class T> uint32_t MapValue(T val)
{
    // default implementation results in a compile-time error
    static_assert(sizeof(T) == 0, "type unavailable: only 32-bit signed/unsigned int and float supported");
    return 0;
}

template<> uint32_t MapValue<uint32_t>(uint32_t val) // 32-bit unsigned int
{
    return val; // no bit-twiddling required, just forward
}

template<> uint32_t MapValue<int32_t>(int32_t val) // 32-bit signed int
{
    return (uint32_t)val^0x80000000; // flip sign bit
}

// taken from Michael Herf's article on Stereopsis
template<> uint32_t MapValue<float>(float val) // 32-bit float
{
    // 1) flip sign bit
    // 2) if sign bit was set flip all other bits as well
    const uint32_t cv = (uint32_t &)val;
    const uint32_t mask = (-(int32_t)(cv>>31))|0x80000000;
    return cv^mask;
}

// LUT optimized binary search implementation for 32-bit POD types
template<class T, size_t LUT_BITS> class SearchPod32
{
public:
    SearchPod32(const std::vector<T> &vals) :
        Vals(vals)
    {
        static_assert(LUT_BITS > 0 && LUT_BITS < 32, "invalid binary search LUT size");
        InitLut();
    }

    ssize_t StdBinarySearch(T key) const
    {
        const auto iter = std::lower_bound(Vals.begin(), Vals.end(), key);
        return (iter != Vals.end() && *iter == key ? std::distance(Vals.begin(), iter) : -1);
    }

    ssize_t MyBinarySearch(T key) const
    {
        return BinarySearch(0, (ssize_t)Vals.size()-1, key);
    }

    ssize_t LutBinarySearch(T key) const
    {
        const auto mappedKey = MapValue<T>(key);
        const auto lutIdx = mappedKey>>(32-LUT_BITS);
        const auto start = Lut[lutIdx];

        // interval end of i-th LUT entry = interval start of (i+1)th LUT entry - 1.
        // however, all LUT remaining LUT entries map to the last valid interval
        // start => interval end < interval start => just use number of values
        const auto end = (lutIdx+1 >= LutEnd ? Vals.size()-1 : Lut[lutIdx+1]-1);
        return BinarySearch(start, end, key);
    }

private:
    ssize_t BinarySearch(ssize_t left, ssize_t right, T key) const
    {
        size_t __len = right-left;
        size_t __first = left;

        while (__len > 0)
        {
            size_t __half = __len >> 1;
            size_t __middle = __first+__half;

            if (Vals[__middle] < key)
            {
                __first = __middle;
                ++__first;

                __len = __len - __half - 1;
            }
            else
                __len = __half;
        }

        return __first;

        /*
        while (left < right)
        {
            const auto mid = left+((right-left)>>1); // avoids overflow
            assert(mid < right); // interval must be reduced in each iteration
            const auto valMid = Vals[mid];

            // no early exit so that always the occurence
            // of the key with the lowest index is found

            if (valMid < key)
                left = mid+1;
            else
                right = mid;
        }
        */

        assert(left == right);
        return (Vals[left] == key ? left : -1);
    }

    void InitLut()
    {
        // fill look-up-table
        Lut.resize((1<<LUT_BITS)+1); // one additional element to avoid condition in interval end computation

        size_t thresh = 0;
        size_t last = 0;

        for (ssize_t i=0; i<(ssize_t)Vals.size()-1; i++)
        {
            const uint32_t mappedNextVal = MapValue<T>(Vals[i+1]);
            const uint32_t nextThresh = mappedNextVal>>(32-LUT_BITS);
            Lut[thresh] = last;

            if (nextThresh > thresh)
            {
                last = i+1;
                for (size_t j=thresh+1; j<=nextThresh; j++)
                    Lut[j] = last;
            }

            thresh = nextThresh;
        }

        // set remaining thresholds that couldn't be found
        for (size_t i=thresh; i<Lut.size()-1; i++)
            Lut[i] = last;

        // remember last end threshold index, because the interval
        // end of all values mapping to an entry bigger than
        // that have to be handled differently
        LutEnd = thresh;

        /*
        for (auto v : Lut)
            std::cout << v.first << ", " << v.second << std::endl;
        */
    }

private:
    std::vector<size_t>    Lut;
    const std::vector<T> & Vals;
    size_t                 LutEnd;
};

template<class T, size_t LUT_BITS, class ALGO_FUNC> void BenchmarkAlgo(const std::vector<T> &vals, const std::vector<T> &keys, const std::string &algoName, const ALGO_FUNC &algoFunc, const SearchPod32<T, LUT_BITS> &s)
{
    std::cout << "Running: '" << algoName << "':" << std::endl;
    std::cout << "---------------------------------------" << std::endl;

    const auto start = std::chrono::high_resolution_clock::now();
    size_t res = 0;

    for (size_t i=0; i<keys.size(); i++)
    {
        const auto idx = (s.*algoFunc)(keys[i]);
        assert(vals[idx] == keys[i]);
        res += idx; // that loop doesn't get optimized out
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const auto elapsed = end-start;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    const auto searchesPerSec = (int)((double)keys.size()/((double)ms/1000.0));

    std::cout << "Result: " << res << std::endl;
    std::cout << "Elapsed time: " << ms << " ms = " << (float)ms/1000.0f << " secs" << std::endl;
    std::cout << "Searches/sec: " << searchesPerSec << " = " << (float)searchesPerSec/1000.0f/1000.0f << " m" << std::endl;
    std::cout << std::endl;
}

template<class T, size_t LUT_BITS, class RND_DIST_VALS> void Benchmark(const std::string &typeDescr, RND_DIST_VALS &distVals)
{
    const size_t NUM_VALS = 1000000000;
    const size_t NUM_KEYS = 10000000;

    std::vector<T> vals(NUM_VALS), keys(NUM_KEYS);
    std::mt19937 gen(303);
    std::uniform_int_distribution<size_t> distKeys(0, vals.size()-1);

    std::cout << "Benchmarking: " << typeDescr << std::endl;
    std::cout << "Generating data set..." << std::endl;

    for (auto &v : vals)
        v = distVals(gen);
    for (auto &k : keys)
        k = vals[distKeys(gen)];

    std::cout << "Pre-sorting data set..." << std::endl << std::endl;
    std::sort(vals.begin(), vals.end()); // sort so that binary search is applicable

    SearchPod32<T, LUT_BITS> s(vals);
    BenchmarkAlgo<T>(vals, keys, "My binary search", &SearchPod32<T, LUT_BITS>::MyBinarySearch, s);
    BenchmarkAlgo<T>(vals, keys, "Standard binary search", &SearchPod32<T, LUT_BITS>::StdBinarySearch, s);
    BenchmarkAlgo<T>(vals, keys, "Lookup binary search", &SearchPod32<T, LUT_BITS>::LutBinarySearch, s);

    std::cout << "=============================================================================" << std::endl << std::endl;
}

template<size_t LUT_BITS> void BenchmarkPods()
{
    std::cout << "=============================================================================" << std::endl;
    std::cout << "Look-up table size: " << LUT_BITS << std::endl;
    std::cout << "=============================================================================" << std::endl << std::endl;

    std::uniform_int_distribution<uint32_t> distIntUnsigned(std::numeric_limits<uint32_t>::min(), std::numeric_limits<uint32_t>::max());
    std::uniform_int_distribution<int32_t> distIntSigned(std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
    std::uniform_real_distribution<float> distFloat(-999.0f, 999.0f);

    Benchmark<uint32_t, LUT_BITS>("Unsigned 32-bit integer", distIntUnsigned);
    Benchmark<int32_t, LUT_BITS>("Signed 32-bit integer", distIntSigned);
    Benchmark<float, LUT_BITS>("32-bit floating point", distFloat);
}

int main(int argc, char **argv)
{
    BenchmarkPods<8>();
    BenchmarkPods<16>();
    BenchmarkPods<24>();
    return 0;
}
