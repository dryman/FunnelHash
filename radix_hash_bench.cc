#include <algorithm>
#include <vector>
#include <utility>
#include <benchmark/benchmark.h>
#include <assert.h>
#include <sys/resource.h>
#include <stdio.h>

#include "radix_hash.h"
#include "strgen.h"
#include "tbb/parallel_sort.h"
#include "pdqsort/pdqsort.h"
#include "config.h"

#ifdef HAVE_PAPI
#include "papi.h"
const int e_num = 3;
int events[e_num] = {
  PAPI_L1_DCM, 
  PAPI_L2_DCM, 
  PAPI_L3_TCM,
};
long long papi_values[e_num];
#endif

struct identity_hash
{
  std::size_t operator()(const int& k) const {
    return k;
  }
};

bool tuple_cmp (std::tuple<std::size_t, int, int> a,
                std::tuple<std::size_t, int, int> b) {
  return std::get<0>(a) < std::get<0>(b);
}

static void BM_radix_hash_bf3_str(benchmark::State& state) {
  int size = state.range(0);
  std::vector<std::tuple<std::size_t, std::string, uint64_t>> dst(size);
  auto src = ::create_strvec(size);
  struct rusage u_before, u_after;
  getrusage(RUSAGE_SELF, &u_before);

#ifdef HAVE_PAPI
  assert(PAPI_start_counters(events, e_num) == PAPI_OK);
#endif

  for (auto _ : state) {
    ::radix_hash_bf3<std::string,uint64_t>(src.begin(),
                                           src.end(), dst.begin(),
                                           ::compute_power(size),
                                           state.range(1), 0);
  }

#ifdef HAVE_PAPI
  assert(PAPI_stop_counters(papi_values, e_num) == PAPI_OK);
  state.counters["L1 miss"] = papi_values[0];
  state.counters["L2 miss"] = papi_values[1];
  state.counters["L3 miss"] = papi_values[2];
#endif

  getrusage(RUSAGE_SELF, &u_after);
  state.counters["Minor"] = u_after.ru_minflt - u_before.ru_minflt;
  state.counters["Major"] = u_after.ru_majflt - u_before.ru_majflt;
  state.counters["Swap"] = u_after.ru_nswap - u_before.ru_nswap;
  state.SetComplexityN(state.range(0));
}

bool str_tuple_cmp (std::tuple<std::size_t, std::string, uint64_t> a,
                    std::tuple<std::size_t, std::string, uint64_t> b) {
  return std::get<0>(a) < std::get<0>(b);
}

static void BM_qsort_string(benchmark::State& state) {
  int size = state.range(0);
  std::vector<std::tuple<std::size_t, std::string, uint64_t>> dst(size);
  auto src = ::create_strvec(size);
  struct rusage u_before, u_after;
  getrusage(RUSAGE_SELF, &u_before);

#ifdef HAVE_PAPI
  assert(PAPI_start_counters(events, e_num) == PAPI_OK);
#endif

  for (auto _ : state) {
    state.PauseTiming();
    for (int i = 0; i < size; i++) {
      std::get<0>(dst[i]) = std::hash<std::string>{}(std::get<0>(src[i]));
      std::get<1>(dst[i]) = std::get<0>(src[i]);
      std::get<2>(dst[i]) = std::get<1>(src[i]);
    }
    state.ResumeTiming();
    std::sort(dst.begin(), dst.end(), str_tuple_cmp);
  }
#ifdef HAVE_PAPI
  assert(PAPI_stop_counters(papi_values, e_num) == PAPI_OK);
  state.counters["L1 miss"] = papi_values[0];
  state.counters["L2 miss"] = papi_values[1];
  state.counters["L3 miss"] = papi_values[2];
#endif

  getrusage(RUSAGE_SELF, &u_after);
  state.SetComplexityN(state.range(0));
  state.counters["Minor"] = u_after.ru_minflt - u_before.ru_minflt;
  state.counters["Major"] = u_after.ru_majflt - u_before.ru_majflt;
  state.counters["Swap"] = u_after.ru_nswap - u_before.ru_nswap;
}

static void BM_radix_hash_bf4_str(benchmark::State& state) {
  int size = state.range(0);
  std::vector<std::tuple<std::size_t, std::string, uint64_t>> dst(size);
  auto src = ::create_strvec(size);
  unsigned int cores = std::thread::hardware_concurrency();
  struct rusage u_before, u_after;
  getrusage(RUSAGE_SELF, &u_before);

#ifdef HAVE_PAPI
  assert(PAPI_start_counters(events, e_num) == PAPI_OK);
#endif

  for (auto _ : state) {
    ::radix_hash_bf4<std::string,uint64_t>(src.begin(),
                                           src.end(), dst.begin(),
                                           ::compute_power(size),
                                           state.range(1), 0, cores);
  }
#ifdef HAVE_PAPI
  assert(PAPI_stop_counters(papi_values, e_num) == PAPI_OK);
  state.counters["L1 miss"] = papi_values[0];
  state.counters["L2 miss"] = papi_values[1];
  state.counters["L3 miss"] = papi_values[2];
#endif

  getrusage(RUSAGE_SELF, &u_after);
  state.SetComplexityN(state.range(0));
  state.counters["Minor"] = u_after.ru_minflt - u_before.ru_minflt;
  state.counters["Major"] = u_after.ru_majflt - u_before.ru_majflt;
  state.counters["Swap"] = u_after.ru_nswap - u_before.ru_nswap;
}

static void BM_radix_hash_bf5_str(benchmark::State& state) {
  int size = state.range(0);
  std::vector<std::tuple<std::size_t, std::string, uint64_t>> dst(size);
  auto src = ::create_strvec(size);
  unsigned int cores = std::thread::hardware_concurrency();
  struct rusage u_before, u_after;
  getrusage(RUSAGE_SELF, &u_before);

#ifdef HAVE_PAPI
  assert(PAPI_start_counters(events, e_num) == PAPI_OK);
#endif

  for (auto _ : state) {
    ::radix_hash_bf5<std::string,uint64_t>(src.begin(),
                                           src.end(), dst.begin(),
                                           ::compute_power(size),
                                           state.range(1), 0, cores);
  }
#ifdef HAVE_PAPI
  assert(PAPI_stop_counters(papi_values, e_num) == PAPI_OK);
  state.counters["L1 miss"] = papi_values[0];
  state.counters["L2 miss"] = papi_values[1];
  state.counters["L3 miss"] = papi_values[2];
#endif

  getrusage(RUSAGE_SELF, &u_after);
  state.SetComplexityN(state.range(0));
  state.counters["Minor"] = u_after.ru_minflt - u_before.ru_minflt;
  state.counters["Major"] = u_after.ru_majflt - u_before.ru_majflt;
  state.counters["Swap"] = u_after.ru_nswap - u_before.ru_nswap;
}

static void BM_radix_hash_bf6_str(benchmark::State& state) {
  int size = state.range(0);
  std::vector<std::tuple<std::size_t, std::string, uint64_t>> dst(size);
  auto src = ::create_strvec(size);
  unsigned int cores = std::thread::hardware_concurrency();
  struct rusage u_before, u_after;
  getrusage(RUSAGE_SELF, &u_before);

#ifdef HAVE_PAPI
  assert(PAPI_start_counters(events, e_num) == PAPI_OK);
#endif

  for (auto _ : state) {
    ::radix_hash_bf6<std::string,uint64_t>(src.begin(),
                                           src.end(), dst.begin(),
                                           state.range(1), 0, cores);
  }
#ifdef HAVE_PAPI
  assert(PAPI_stop_counters(papi_values, e_num) == PAPI_OK);
  state.counters["L1 miss"] = papi_values[0];
  state.counters["L2 miss"] = papi_values[1];
  state.counters["L3 miss"] = papi_values[2];
#endif

  getrusage(RUSAGE_SELF, &u_after);

  state.SetComplexityN(state.range(0));
  state.counters["Minor"] = u_after.ru_minflt - u_before.ru_minflt;
  state.counters["Major"] = u_after.ru_majflt - u_before.ru_majflt;
  state.counters["Swap"] = u_after.ru_nswap - u_before.ru_nswap;
}

static void BM_tbb_sort_string(benchmark::State& state) {
  int size = state.range(0);
  std::vector<std::tuple<std::size_t, std::string, uint64_t>> dst(size);
  auto src = ::create_strvec(size);
  struct rusage u_before, u_after;
  getrusage(RUSAGE_SELF, &u_before);

#ifdef HAVE_PAPI
  assert(PAPI_start_counters(events, e_num) == PAPI_OK);
#endif

  for (auto _ : state) {
    state.PauseTiming();
    for (int i = 0; i < size; i++) {
      std::get<0>(dst[i]) = std::hash<std::string>{}(std::get<0>(src[i]));
      std::get<1>(dst[i]) = std::get<0>(src[i]);
      std::get<2>(dst[i]) = std::get<1>(src[i]);
    }
    state.ResumeTiming();
    tbb::parallel_sort(dst.begin(), dst.end(), str_tuple_cmp);
  }
#ifdef HAVE_PAPI
  assert(PAPI_stop_counters(papi_values, e_num) == PAPI_OK);
  state.counters["L1 miss"] = papi_values[0];
  state.counters["L2 miss"] = papi_values[1];
  state.counters["L3 miss"] = papi_values[2];
#endif

  getrusage(RUSAGE_SELF, &u_after);
  state.SetComplexityN(state.range(0));
  state.counters["Minor"] = u_after.ru_minflt - u_before.ru_minflt;
  state.counters["Major"] = u_after.ru_majflt - u_before.ru_majflt;
  state.counters["Swap"] = u_after.ru_nswap - u_before.ru_nswap;
}

static void BM_pdqsort_string(benchmark::State& state) {
  int size = state.range(0);
  std::vector<std::tuple<std::size_t, std::string, uint64_t>> dst(size);
  auto src = ::create_strvec(size);
  struct rusage u_before, u_after;
  getrusage(RUSAGE_SELF, &u_before);

#ifdef HAVE_PAPI
  assert(PAPI_start_counters(events, e_num) == PAPI_OK);
#endif

  for (auto _ : state) {
    state.PauseTiming();
    for (int i = 0; i < size; i++) {
      std::get<0>(dst[i]) = std::hash<std::string>{}(std::get<0>(src[i]));
      std::get<1>(dst[i]) = std::get<0>(src[i]);
      std::get<2>(dst[i]) = std::get<1>(src[i]);
    }
    state.ResumeTiming();
    pdqsort(dst.begin(), dst.end(), str_tuple_cmp);
  }
#ifdef HAVE_PAPI
  assert(PAPI_stop_counters(papi_values, e_num) == PAPI_OK);
  state.counters["L1 miss"] = papi_values[0];
  state.counters["L2 miss"] = papi_values[1];
  state.counters["L3 miss"] = papi_values[2];
#endif

  getrusage(RUSAGE_SELF, &u_after);

  state.SetComplexityN(state.range(0));
  state.counters["Minor"] = u_after.ru_minflt - u_before.ru_minflt;
  state.counters["Major"] = u_after.ru_majflt - u_before.ru_majflt;
  state.counters["Swap"] = u_after.ru_nswap - u_before.ru_nswap;
}

static void RadixArguments(benchmark::internal::Benchmark* b) {
  for (int i = 11; i < 12; i+=1) {
    for (int j = (1 << 18); j <= (1 << 23); j <<= 1) {
      b->Args({j, i});
    }
  }
}

static void BM_bf6_1_thread(benchmark::State& state) {
  int size = state.range(0);
  std::vector<std::tuple<std::size_t, std::string, uint64_t>> dst(size);
  auto src = ::create_strvec(size);
  struct rusage u_before, u_after;
  getrusage(RUSAGE_SELF, &u_before);

#ifdef HAVE_PAPI
  assert(PAPI_start_counters(events, e_num) == PAPI_OK);
#endif

  for (auto _ : state) {
    ::radix_hash_bf6<std::string,uint64_t>(src.begin(),
                                           src.end(), dst.begin(),
                                           state.range(1), 0, 1);
  }
#ifdef HAVE_PAPI
  assert(PAPI_stop_counters(papi_values, e_num) == PAPI_OK);
  state.counters["L1 miss"] = papi_values[0];
  state.counters["L2 miss"] = papi_values[1];
  state.counters["L3 miss"] = papi_values[2];
#endif

  getrusage(RUSAGE_SELF, &u_after);

  state.SetComplexityN(state.range(0));
  state.counters["Minor"] = u_after.ru_minflt - u_before.ru_minflt;
  state.counters["Major"] = u_after.ru_majflt - u_before.ru_majflt;
  state.counters["Swap"] = u_after.ru_nswap - u_before.ru_nswap;
}

static void BM_bf7(benchmark::State& state) {
  int size = state.range(0);
  std::vector<std::tuple<std::size_t, std::string, uint64_t>> dst(size);
  auto src = ::create_strvec(size);
  struct rusage u_before, u_after;
  getrusage(RUSAGE_SELF, &u_before);

#ifdef HAVE_PAPI
  assert(PAPI_start_counters(events, e_num) == PAPI_OK);
#endif

  for (auto _ : state) {
    state.PauseTiming();
    for (int i = 0; i < size; i++) {
      std::get<0>(dst[i]) = std::hash<std::string>{}(std::get<0>(src[i]));
      std::get<1>(dst[i]) = std::get<0>(src[i]);
      std::get<2>(dst[i]) = std::get<1>(src[i]);
    }
    state.ResumeTiming();
    ::radix_hash_bf7<std::string,uint64_t>(dst.begin(),
                                           state.range(0), state.range(1));
  }
#ifdef HAVE_PAPI
  assert(PAPI_stop_counters(papi_values, e_num) == PAPI_OK);
  state.counters["L1 miss"] = papi_values[0];
  state.counters["L2 miss"] = papi_values[1];
  state.counters["L3 miss"] = papi_values[2];
#endif

  getrusage(RUSAGE_SELF, &u_after);

  state.SetComplexityN(state.range(0));
  state.counters["Minor"] = u_after.ru_minflt - u_before.ru_minflt;
  state.counters["Major"] = u_after.ru_majflt - u_before.ru_majflt;
  state.counters["Swap"] = u_after.ru_nswap - u_before.ru_nswap;
}

static void BM_bf8(benchmark::State& state) {
  int size = state.range(0);
  std::vector<std::tuple<std::size_t, std::string, uint64_t>> dst(size);
  unsigned int cores = std::thread::hardware_concurrency();
  auto src = ::create_strvec(size);
  struct rusage u_before, u_after;
  getrusage(RUSAGE_SELF, &u_before);

#ifdef HAVE_PAPI
  assert(PAPI_start_counters(events, e_num) == PAPI_OK);
#endif

  for (auto _ : state) {
    state.PauseTiming();
    for (int i = 0; i < size; i++) {
      std::get<0>(dst[i]) = std::hash<std::string>{}(std::get<0>(src[i]));
      std::get<1>(dst[i]) = std::get<0>(src[i]);
      std::get<2>(dst[i]) = std::get<1>(src[i]);
    }
    state.ResumeTiming();
    ::radix_hash_bf8<std::string,uint64_t>(dst.begin(),
                                           state.range(0),
                                           state.range(1), cores);
  }
#ifdef HAVE_PAPI
  assert(PAPI_stop_counters(papi_values, e_num) == PAPI_OK);
  state.counters["L1 miss"] = papi_values[0];
  state.counters["L2 miss"] = papi_values[1];
  state.counters["L3 miss"] = papi_values[2];
#endif

  getrusage(RUSAGE_SELF, &u_after);

  state.SetComplexityN(state.range(0));
  state.counters["Minor"] = u_after.ru_minflt - u_before.ru_minflt;
  state.counters["Major"] = u_after.ru_majflt - u_before.ru_majflt;
  state.counters["Swap"] = u_after.ru_nswap - u_before.ru_nswap;
}

/*
 * Note: breadth first search outperforms depth first search when
 * input is small. For very large input, depth first wins a little bit.
 */

BENCHMARK(BM_qsort_string)->Apply(RadixArguments)
->Complexity(benchmark::oN)->UseRealTime();
BENCHMARK(BM_tbb_sort_string)->Apply(RadixArguments)
->Complexity(benchmark::oN)->UseRealTime();
BENCHMARK(BM_pdqsort_string)->Apply(RadixArguments)
->Complexity(benchmark::oN)->UseRealTime();
BENCHMARK(BM_radix_hash_bf3_str)->Apply(RadixArguments)
->Complexity(benchmark::oN)->UseRealTime();
BENCHMARK(BM_radix_hash_bf4_str)->Apply(RadixArguments)
->Complexity(benchmark::oN)->UseRealTime();
BENCHMARK(BM_radix_hash_bf5_str)->Apply(RadixArguments)
->Complexity(benchmark::oN)->UseRealTime();
BENCHMARK(BM_radix_hash_bf6_str)->Apply(RadixArguments)
->Complexity(benchmark::oN)->UseRealTime();

BENCHMARK(BM_bf6_1_thread)->Apply(RadixArguments)
->Complexity(benchmark::oN)->UseRealTime();

BENCHMARK(BM_bf7)->Apply(RadixArguments)
->Complexity(benchmark::oN)->UseRealTime();

BENCHMARK(BM_bf8)->Apply(RadixArguments)
->Complexity(benchmark::oN)->UseRealTime();

BENCHMARK_MAIN();
