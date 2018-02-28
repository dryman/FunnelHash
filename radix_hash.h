#ifndef RADIX_HASH_H
#define RADIX_HASH_H 1

#include <iostream>
#include <iterator>
#include <utility>
#include <vector>
#include <functional>
#include <sys/mman.h>

// namespace radix_hash?

static inline
int compute_power(int input_num) {
  return 64 - __builtin_clzll(input_num);
}

static inline
std::size_t compute_mask(int input_num) {
  return (1ULL << ::compute_power(input_num)) - 1;
}

template<typename Key,
  typename Value>
  bool HashTupleCmp(std::tuple<std::size_t, Key, Value>const& a,
                    std::tuple<std::size_t, Key, Value>const& b,
                    std::size_t mask) {
  std::size_t a_hash, b_hash, a_mask_hash, b_mask_hash;
  a_hash = std::get<0>(a);
  b_hash = std::get<0>(b);
  a_mask_hash = a_hash & mask;
  b_mask_hash = b_hash & mask;
  if (a_mask_hash != b_mask_hash) {
    return a_mask_hash < b_mask_hash;
  }
  if (a_hash != b_hash) {
    return a_hash < b_hash;
  }
  return std::get<1>(a) < std::get<1>(b);
}

template<typename Key,
  typename Value>
  bool HashTupleEquiv(std::tuple<std::size_t, Key, Value>const& a,
                    std::tuple<std::size_t, Key, Value>const& b) {
  return std::get<0>(a) == std::get<0>(b) &&
    std::get<1>(a) == std::get<1>(b);
}

// TODO: Should benchmark the speed difference between array or vector.
// TODO: use boolean template to create an export_hash variant
// TODO: use integer key and identity hash function for unit testing
template <typename Key,
  typename Value,
  typename Hash = std::hash<Key>,
  typename BidirectionalIterator,
  typename RandomAccessIterator>
  void radix_hash_df1(BidirectionalIterator begin,
      BidirectionalIterator end,
      RandomAccessIterator dst,
      int mask_bits,
      int partition_bits,
      int nosort_bits) {
  int input_num, num_iter, shift, dst_idx, prev_idx;
  int partitions = 1 << partition_bits;
  std::size_t h, mask;

  input_num = std::distance(begin, end);

  if (mask_bits <= nosort_bits) {
    for (auto iter = begin; iter != end; iter++) {
      h = Hash{}(std::get<0>(*iter));
      std::get<0>(*dst) = h;
      std::get<1>(*dst) = iter->first;
      std::get<2>(*dst) = iter->second;
      ++dst;
    }
    return;
  }

  num_iter = (mask_bits - nosort_bits
              + partition_bits - 1) / partition_bits;
  mask = (1ULL << mask_bits) - 1;
  shift = mask_bits - partition_bits;
  shift = shift < 0 ? 0 : shift;

  // invariant: counters[partitions] is the total count
  int counters[partitions + 1];
  int counter_stack[num_iter - 1][partitions + 1];
  int iter_stack[num_iter - 1];

  for (int i = 0; i < partitions + 1; i++)
    counters[i] = 0;
  for (int i = 0; i < num_iter - 1; i++)
    for (int j = 0; j < partitions + 1; j++)
      counter_stack[i][j] = 0;
  for (int i = 0; i < num_iter - 1; i++)
    iter_stack[i] = 0;

  std::unique_ptr<std::tuple<std::size_t, Key, Value>[]> buffers[2];
  buffers[0] = std::make_unique<
  std::tuple<std::size_t, Key, Value>[]>(input_num);
  buffers[1] = std::make_unique<
  std::tuple<std::size_t, Key, Value>[]>(input_num);

  for (auto iter = begin; iter != end; ++iter) {
    h = Hash{}(std::get<0>(*iter));
    counters[(h & mask) >> shift]++;
  }
  for (int i = 1; i < partitions; i++) {
    counters[i] += counters[i - 1];
  }
  for (int i = partitions; i != 0; i--) {
    counters[i] = counters[i - 1];
  }
  counters[0] = 0;
  for (int i = 0; i < partitions + 1; i++)
    counter_stack[0][i] = counters[i];

  for (auto iter = begin; iter != end; ++iter) {
    h = Hash{}(std::get<0>(*iter));
    // TODO make this a template inline function
    dst_idx = counters[(h & mask) >> shift]++;
    if (num_iter == 1) {
      std::get<0>(dst[dst_idx]) = h;
      std::get<1>(dst[dst_idx]) = iter->first;
      std::get<2>(dst[dst_idx]) = iter->second;
    } else {
      std::get<0>(buffers[0][dst_idx]) = h;
      std::get<1>(buffers[0][dst_idx]) = iter->first;
      std::get<2>(buffers[0][dst_idx]) = iter->second;
    }
  }
  if (num_iter == 1) return;


  for (int i = 0; true;) {
    if (counter_stack[i][iter_stack[i]] ==
        counter_stack[i][partitions]) {
      if (i == 0) {
        return;
      }
      iter_stack[i] = 0;
      iter_stack[i - 1]++;
      i--;
      continue;
    }
    mask = (1ULL << (mask_bits - partition_bits * (i + 1))) - 1;
    shift = mask_bits - partition_bits * (i + 2);
    shift = shift < 0 ? 0 : shift;

    // clear counters
    for (int j = 0; j < partitions; j++)
      counters[j] = 0;
    // accumulate counters
    for (int j = counter_stack[i][iter_stack[i]];
         j < counter_stack[i][iter_stack[i] + 1];
         j++) {
      h = std::get<0>(buffers[i & 1][j]);
      counters[(h & mask) >> shift]++;
    }
    prev_idx = counter_stack[i][iter_stack[i]];
    for (int j = 1; j < partitions; j++) {
      counters[j] += counters[j - 1];
    }
    for (int j = partitions; j != 0; j--) {
      counters[j] = counters[j - 1] + prev_idx;;
    }
    counters[0] = prev_idx;

    if (i < num_iter - 2) {
      for (int j = 0; j < partitions + 1; j++)
        counter_stack[i + 1][j] = counters[j];
      for (int j = counter_stack[i][iter_stack[i]];
           j < counter_stack[i][iter_stack[i] + 1];
           j++) {
        h = std::get<0>(buffers[i & 1][j]);
        buffers[(i + 1) & 1][counters[(h & mask) >> shift]++] =
          buffers[i & 1][j];
      }
      i++;
    } else {
      for (int j = counter_stack[i][iter_stack[i]];
           j < counter_stack[i][iter_stack[i] + 1];
           j++) {
        h = std::get<0>(buffers[i & 1][j]);
        dst_idx = counters[(h & mask) >> shift]++;
        *(dst + dst_idx) = buffers[i & 1][j];
      }
      iter_stack[i]++;
    }
  }
}

template <typename Key,
  typename Value,
  typename Hash = std::hash<Key>,
  typename BidirectionalIterator,
  typename RandomAccessIterator>
  void radix_hash_df2(BidirectionalIterator begin,
      BidirectionalIterator end,
      RandomAccessIterator dst,
      int mask_bits,
      int partition_bits,
      int nosort_bits) {
  int input_num, num_iter, shift, dst_idx, prev_idx;
  int partitions = 1 << partition_bits;
  std::size_t h, mask;

  input_num = std::distance(begin, end);

  if (mask_bits <= nosort_bits) {
    for (auto iter = begin; iter != end; iter++) {
      h = Hash{}(std::get<0>(*iter));
      std::get<0>(*dst) = h;
      std::get<1>(*dst) = iter->first;
      std::get<2>(*dst) = iter->second;
      ++dst;
    }
    return;
  }

  num_iter = (mask_bits - nosort_bits
              + partition_bits - 1) / partition_bits;
  mask = (1ULL << mask_bits) - 1;
  shift = mask_bits - partition_bits;
  shift = shift < 0 ? 0 : shift;

  // invariant: counters[partitions] is the total count
  int counters[partitions + 1];
  int counter_stack[num_iter][partitions + 1];
  int iter_stack[num_iter];

  for (int i = 0; i < partitions + 1; i++)
    counters[i] = 0;
  for (int i = 0; i < num_iter; i++)
    for (int j = 0; j < partitions + 1; j++)
      counter_stack[i][j] = 0;
  for (int i = 0; i < num_iter; i++)
    iter_stack[i] = 0;

  std::unique_ptr<std::tuple<std::size_t, Key, Value>[]> buffers[2];
  buffers[0] = std::make_unique<
    std::tuple<std::size_t, Key, Value>[]>(input_num);
  buffers[1] = std::make_unique<
    std::tuple<std::size_t, Key, Value>[]>(input_num);

  // Major difference to previous hash_sort: we calculate hash
  // value only once, but do an extra copy. Copy is likely to be cheaper
  // than hashing.
  dst_idx = 0;
  for (auto iter = begin; iter != end; ++iter) {
    h = Hash{}(std::get<0>(*iter));
    std::get<0>(buffers[1][dst_idx]) = h;
    std::get<1>(buffers[1][dst_idx]) = iter->first;
    std::get<2>(buffers[1][dst_idx]) = iter->second;
    dst_idx++;
  }

  for (int i = 0; i < input_num; i++) {
    h = std::get<0>(buffers[1][i]);
    counters[(h & mask) >> shift]++;
  }
  for (int i = 1; i < partitions; i++) {
    counters[i] += counters[i - 1];
  }
  for (int i = partitions; i != 0; i--) {
    counters[i] = counters[i - 1];
  }
  counters[0] = 0;
  for (int i = 0; i < partitions + 1; i++)
    counter_stack[0][i] = counters[i];

  if (num_iter == 1) {
    for (int i = 0; i < input_num; i++) {
      h = std::get<0>(buffers[1][i]);
      dst_idx = counters[(h & mask) >> shift]++;
      *(dst + dst_idx) = buffers[1][i];
    }
    return;
  } else {
    for (int i = 0; i < input_num; i++) {
      h = std::get<0>(buffers[1][i]);
      buffers[0][counters[(h & mask) >> shift]++] = buffers[1][i];
    }
  }

  for (int i = 0; true;) {
    if (counter_stack[i][iter_stack[i]] ==
        counter_stack[i][partitions]) {
      if (i == 0) {
        return;
      }
      iter_stack[i] = 0;
      iter_stack[i - 1]++;
      i--;
      continue;
    }
    mask = (1ULL << (mask_bits - partition_bits * (i + 1))) - 1;
    shift = mask_bits - partition_bits * (i + 2);
    shift = shift < 0 ? 0 : shift;

    // clear counters
    for (int j = 0; j < partitions; j++)
      counters[j] = 0;
    // accumulate counters
    for (int j = counter_stack[i][iter_stack[i]];
         j < counter_stack[i][iter_stack[i] + 1];
         j++) {
      h = std::get<0>(buffers[i & 1][j]);
      counters[(h & mask) >> shift]++;
    }
    prev_idx = counter_stack[i][iter_stack[i]];
    for (int j = 1; j < partitions; j++) {
      counters[j] += counters[j - 1];
    }
    for (int j = partitions; j != 0; j--) {
      counters[j] = counters[j - 1] + prev_idx;;
    }
    counters[0] = prev_idx;

    if (i < num_iter - 2) {
      for (int j = 0; j < partitions + 1; j++)
        counter_stack[i + 1][j] = counters[j];
      for (int j = counter_stack[i][iter_stack[i]];
           j < counter_stack[i][iter_stack[i] + 1];
           j++) {
        h = std::get<0>(buffers[i & 1][j]);
        buffers[(i + 1) & 1][counters[(h & mask) >> shift]++] =
          buffers[i & 1][j];
      }
      i++;
    } else {
      for (int j = counter_stack[i][iter_stack[i]];
           j < counter_stack[i][iter_stack[i] + 1];
           j++) {
        h = std::get<0>(buffers[i & 1][j]);
        dst_idx = counters[(h & mask) >> shift]++;
        *(dst + dst_idx) = buffers[i & 1][j];
      }
      iter_stack[i]++;
    }
  }
}

template <typename Key,
  typename Value,
  typename Hash = std::hash<Key>,
  typename BidirectionalIterator,
  typename RandomAccessIterator>
  void radix_hash_bf1(BidirectionalIterator begin,
      BidirectionalIterator end,
      RandomAccessIterator dst,
      int mask_bits,
      int partition_bits,
      int nosort_bits) {
  int input_num, num_iter, iter, shift, shift1, shift2, dst_idx, anchor_idx;
  int partitions = 1 << partition_bits;
  std::size_t h, h1, h2, h_cnt, mask, mask1, mask2, anchor_h1;

  input_num = std::distance(begin, end);

  if (mask_bits <= nosort_bits) {
    for (auto iter = begin; iter != end; iter++) {
      h = Hash{}(std::get<0>(*iter));
      std::get<0>(*dst) = h;
      std::get<1>(*dst) = iter->first;
      std::get<2>(*dst) = iter->second;
      ++dst;
    }
    return;
  }

  num_iter = (mask_bits - nosort_bits
              + partition_bits - 1) / partition_bits;
  mask = (1ULL << mask_bits) - 1;
  shift = mask_bits - partition_bits;
  shift = shift < 0 ? 0 : shift;

  // invariant: counters[partitions] is the total count
  int counters[partitions];
  int flush_counters[partitions];
  int idx_counters[partitions];

  for (int i = 0; i < partitions + 1; i++)
    counters[i] = 0;

  std::unique_ptr<std::tuple<std::size_t, Key, Value>[]> buffers[2];
  buffers[0] = std::make_unique<
  std::tuple<std::size_t, Key, Value>[]>(input_num);
  buffers[1] = std::make_unique<
  std::tuple<std::size_t, Key, Value>[]>(input_num);

  /*
  madvise(&*begin, input_num * (sizeof(begin->first) + sizeof(begin->second)),
          MADV_SEQUENTIAL);
  madvise(&buffers[0], input_num *
          (sizeof(begin->first) + sizeof(begin->second) + sizeof(std::size_t)),
          MADV_SEQUENTIAL);
  madvise(&buffers[1], input_num *
          (sizeof(begin->first) + sizeof(begin->second) + sizeof(std::size_t)),
          MADV_SEQUENTIAL);
  */

  for (auto iter = begin; iter != end; ++iter) {
    h = Hash{}(std::get<0>(*iter));
    counters[(h & mask) >> shift]++;
  }
  for (int i = 1; i < partitions; i++) {
    counters[i] += counters[i - 1];
  }
  for (int i = partitions - 1; i != 0; i--) {
    counters[i] = counters[i - 1];
  }
  counters[0] = 0;

  if (num_iter == 1) {
    for (auto iter = begin; iter != end; ++iter) {
      h = Hash{}(std::get<0>(*iter));
      dst_idx = counters[(h & mask) >> shift]++;
      std::get<0>(dst[dst_idx]) = h;
      std::get<1>(dst[dst_idx]) = std::move(iter->first);
      std::get<2>(dst[dst_idx]) = std::move(iter->second);
    }
    return;
  } else {
    for (auto iter = begin; iter != end; ++iter) {
      h = Hash{}(std::get<0>(*iter));
      dst_idx = counters[(h & mask) >> shift]++;
      std::get<0>(buffers[0][dst_idx]) = h;
      std::get<1>(buffers[0][dst_idx]) = std::move(iter->first);
      std::get<2>(buffers[0][dst_idx]) = std::move(iter->second);
    }
  }

  madvise(&*begin, input_num * (sizeof(begin->first) + sizeof(begin->second)),
          MADV_DONTNEED);

  for (iter = 0; iter < num_iter - 2; iter++) {
    mask1 = (1ULL << (mask_bits - partition_bits * iter)) - 1;
    shift1 = mask_bits - partition_bits * (iter + 1);
    shift1 = shift1 < 0 ? 0 : shift1;
    mask2 = (1ULL << (mask_bits - partition_bits * (iter + 1))) - 1;
    shift2 = mask_bits - partition_bits * (iter + 2);
    shift2 = shift2 < 0 ? 0 : shift2;
    for (int k = 0; k < partitions; k++) {
      counters[k] = 0;
    }
    anchor_h1 = (std::get<0>(buffers[iter & 1][0]) & mask1) >> shift1;
    anchor_idx = 0;

    for (int j = 0; j < input_num; j++) {
      h = std::get<0>(buffers[iter & 1][j]);
      h1 = (h & mask1) >> shift1;
      h2 = (h & mask2) >> shift2;
      if (h1 == anchor_h1) {
        counters[h2]++;
      } else {
        // flush counters
        idx_counters[0] = anchor_idx;
        for (unsigned int k = 1; k < partitions; k++) {
          idx_counters[k] = idx_counters[k-1] + counters[k-1];
        }
        for (int k = anchor_idx; k < j; k++) {
          h = std::get<0>(buffers[iter & 1][k]);
          buffers[(iter + 1) & 1][idx_counters[(h & mask2) >> shift2]++] =
            std::move(buffers[iter & 1][k]);
        }
        // clear up counters and reset anchor
        for (int k = 0; k < partitions; k++) {
          counters[k] = 0;
        }
        anchor_idx = j;
        anchor_h1 = h1;
        counters[h2]++;
      }
    }
    // flush counters
    idx_counters[0] = anchor_idx;
    for (unsigned int k = 1; k < partitions; k++) {
      idx_counters[k] = idx_counters[k-1] + counters[k-1];
    }
    for (int k = anchor_idx; k < input_num; k++) {
      h = std::get<0>(buffers[iter & 1][k]);
      buffers[(iter + 1) & 1][idx_counters[(h & mask2) >> shift2]++] =
      std::move(buffers[iter & 1][k]);
    }
  }

  madvise(&*dst, input_num *
          (sizeof(begin->first) + sizeof(begin->second) + sizeof(std::size_t)),
          MADV_WILLNEED);

  // flush the last buffer to dst.
  mask1 = (1ULL << (mask_bits - partition_bits * iter)) - 1;
  shift1 = mask_bits - partition_bits * (iter + 1);
  shift1 = shift1 < 0 ? 0 : shift1;
  mask2 = (1ULL << (mask_bits - partition_bits * (iter + 1))) - 1;
  shift2 = mask_bits - partition_bits * (iter + 2);
  shift2 = shift2 < 0 ? 0 : shift2;

  for (int k = 0; k < partitions; k++) {
    counters[k] = 0;
  }
  anchor_h1 = (std::get<0>(buffers[iter & 1][0]) & mask1) >> shift1;
  anchor_idx = 0;
  for (int j = 0; j < input_num; j++) {
    h = std::get<0>(buffers[iter & 1][j]);
    h1 = (h & mask1) >> shift1;
    h2 = (h & mask2) >> shift2;
    if (h1 == anchor_h1) {
      counters[h2]++;
    } else {
      // flush counters
      for (int k = 0; k < partitions; k++) {
        flush_counters[k] = 0;
      }
      idx_counters[0] = anchor_idx;
      for (unsigned int k = 1; k < partitions; k++) {
        idx_counters[k] = idx_counters[k-1] + counters[k-1];
      }

      for (int k = anchor_idx; k < j; k++) {
        h = std::get<0>(buffers[iter & 1][k]);
        h_cnt = (h & mask2) >> shift2;
        dst_idx = idx_counters[h_cnt]++;
        flush_counters[h_cnt]++;
        *(dst + dst_idx) = std::move(buffers[iter & 1][k]);
        if (nosort_bits == 0) {
          for (int m = flush_counters[h_cnt]; m < counters[h_cnt]; m++) {
            if (HashTupleCmp(*(dst + dst_idx), *(dst + dst_idx - 1), mask)) {
              std::swap(*(dst + dst_idx), *(dst + dst_idx - 1));
              dst_idx--;
            } else break;
          }
        }
      }
      // clear up counters and reset anchor
      for (int k = 0; k < partitions; k++) {
        counters[k] = 0;
      }
      anchor_idx = j;
      anchor_h1 = h1;
      counters[h2]++;
    }
  }
  // flush counters
  for (int k = 0; k < partitions; k++) {
    flush_counters[k] = 0;
  }
  idx_counters[0] = anchor_idx;
  for (unsigned int k = 1; k < partitions; k++) {
    idx_counters[k] = idx_counters[k-1] + counters[k-1];
  }

  for (int k = anchor_idx; k < input_num; k++) {
    h = std::get<0>(buffers[iter & 1][k]);
    h_cnt = (h & mask2) >> shift2;
    dst_idx = idx_counters[h_cnt]++;
    flush_counters[h_cnt]++;
    *(dst + dst_idx) = std::move(buffers[iter & 1][k]);
    if (nosort_bits == 0) {
      for (int m = flush_counters[h_cnt]; m < counters[h_cnt]; m++) {
        if (HashTupleCmp(*(dst + dst_idx), *(dst + dst_idx - 1), mask)) {
          std::swap(*(dst + dst_idx), *(dst + dst_idx - 1));
          dst_idx--;
        } else break;
      }
    }
  }
}

struct RadixCounter {
  enum Status {
    ToFill,
    Normal,
    End,
  };
  int idx = 0;
  int end = 0;
  Status status = Normal;
};

template <typename Key,
  typename Value,
  typename Hash = std::hash<Key>,
  typename BidirectionalIterator,
  typename RandomAccessIterator>
  void radix_hash_bf2(BidirectionalIterator begin,
      BidirectionalIterator end,
      RandomAccessIterator dst,
      int mask_bits,
      int partition_bits,
      int nosort_bits) {
  int input_num, num_iter, iter, shift, shift1, shift2, dst_idx, anchor_idx;
  int partitions = 1 << partition_bits;
  std::size_t h, h1, h2, h_cnt, mask, mask1, mask2, anchor_h1;

  input_num = std::distance(begin, end);

  if (mask_bits <= nosort_bits) {
    for (auto iter = begin; iter != end; iter++) {
      h = Hash{}(std::get<0>(*iter));
      std::get<0>(*dst) = h;
      std::get<1>(*dst) = iter->first;
      std::get<2>(*dst) = iter->second;
      ++dst;
    }
    return;
  }

  num_iter = (mask_bits - nosort_bits
              + partition_bits - 1) / partition_bits;
  mask = (1ULL << mask_bits) - 1;
  shift = mask_bits - partition_bits;
  shift = shift < 0 ? 0 : shift;

  // invariant: counters[partitions] is the total count
  int counters[partitions];
  int flush_counters[partitions];
  int idx_counters[partitions];
  std::vector<RadixCounter> radix_counter(partitions);

  for (int i = 0; i < partitions + 1; i++)
    counters[i] = 0;

  for (auto iter = begin; iter != end; ++iter) {
    h = Hash{}(std::get<0>(*iter));
    counters[(h & mask) >> shift]++;
  }
  for (int i = 1; i < partitions; i++) {
    counters[i] += counters[i - 1];
  }
  for (int i = partitions - 1; i != 0; i--) {
    counters[i] = counters[i - 1];
  }
  counters[0] = 0;

  for (auto iter = begin; iter != end; ++iter) {
    h = Hash{}(std::get<0>(*iter));
    dst_idx = counters[(h & mask) >> shift]++;
    std::get<0>(dst[dst_idx]) = h;
    std::get<1>(dst[dst_idx]) = std::move(iter->first);
    std::get<2>(dst[dst_idx]) = std::move(iter->second);
  }

  for (iter = 0; iter < num_iter - 1; iter++) {
    mask1 = (1ULL << (mask_bits - partition_bits * iter)) - 1;
    shift1 = mask_bits - partition_bits * (iter + 1);
    shift1 = shift1 < 0 ? 0 : shift1;
    mask2 = (1ULL << (mask_bits - partition_bits * (iter + 1))) - 1;
    shift2 = mask_bits - partition_bits * (iter + 2);
    shift2 = shift2 < 0 ? 0 : shift2;
    for (int k = 0; k < partitions; k++) {
      counters[k] = 0;
    }
    anchor_h1 = (std::get<0>(dst[0]) & mask1) >> shift1;
    anchor_idx = 0;

    for (int j = 0; j < input_num; j++) {
      h = std::get<0>(dst[j]);
      h1 = (h & mask1) >> shift1;
      h2 = (h & mask2) >> shift2;
      if (h1 == anchor_h1) {
        counters[h2]++;
      } else {
        // flush counters
        for (int k = 0; k < partitions; k++) {
          flush_counters[k] = 0;
        }
        radix_counter[0] = {anchor_idx, anchor_idx + counters[0],
                            RadixCounter::Status::Normal};
        for (unsigned int k = 1; k < partitions; k++) {
          int prev_end = radix_counter[k - 1].end;
          radix_counter[k] = {prev_end, prev_end + counters[k],
                              RadixCounter::Status::Normal};
        }
        int ctr = 0;
        std::tuple<std::size_t, Key, Value> tmp;
        while (ctr < partitions) {
          if (radix_counter[ctr].idx == radix_counter[ctr].end) {
            ctr++;
            continue;
          }

        }
        
        /*
        for (int k = anchor_idx; k < j; k++) {
          h = std::get<0>(buffers[iter & 1][k]);
          buffers[(iter + 1) & 1][idx_counters[(h & mask2) >> shift2]++] =
            std::move(buffers[iter & 1][k]);
        }
        */
        // clear up counters and reset anchor
        for (int k = 0; k < partitions; k++) {
          counters[k] = 0;
        }
        anchor_idx = j;
        anchor_h1 = h1;
        counters[h2]++;
      }
    }
    // flush counters
    /*
    idx_counters[0] = anchor_idx;
    for (unsigned int k = 1; k < partitions; k++) {
      idx_counters[k] = idx_counters[k-1] + counters[k-1];
    }
    for (int k = anchor_idx; k < input_num; k++) {
      h = std::get<0>(buffers[iter & 1][k]);
      buffers[(iter + 1) & 1][idx_counters[(h & mask2) >> shift2]++] =
      std::move(buffers[iter & 1][k]);
    }
    */
  }

}

#endif
