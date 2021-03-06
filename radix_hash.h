/*
 * Copyright 2018 Felix Chern
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef RADIX_HASH_H
#define RADIX_HASH_H 1

#include <iostream>
#include <iterator>
#include <utility>
#include <vector>
#include <functional>
#include <sys/mman.h>
#include <memory>
#include <assert.h>
#include <atomic>
#include <thread>
#include <cmath>
#include <mutex>
#include "thread_barrier.h"

// namespace radix_hash?
namespace radix_hash {

// returns partition bits
static inline int
optimal_partition(std::size_t input_num) {
  double min_dist = 1.0;
  int candidate = 0;
  for (int k = 6; k < 15; k++) {
    double log_k_input = log(input_num)/log(1<<k);
    double top = ceil(log_k_input);
    double bottom = floor(log_k_input);
    double dist = log_k_input - bottom < top - log_k_input ?
     log_k_input - bottom : top - log_k_input;
    if (input_num < (1ULL<<k)) {
      return k;
    }
    if (dist <= min_dist) {
      candidate = k;
      min_dist = dist;
    }
  }
  return candidate;
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

template<typename RandomAccessIterator>
static inline
void bf6_insertion_inner(RandomAccessIterator dst,
                         std::size_t idx,
                         std::size_t limit) {
  std::size_t h1, h2;
  while (idx > limit) {
    h1 = std::get<0>(dst[idx]);
    h2 = std::get<0>(dst[idx-1]);
    if (h1 > h2)
      break;
    if (h1 < h2) {
      std::swap(dst[idx], dst[idx-1]);
      idx--;
      continue;
    }
    if (std::get<1>(dst[idx]) < std::get<1>(dst[idx-1])) {
      std::swap(dst[idx], dst[idx-1]);
      idx--;
      continue;
    }
    break;
  }
}

template<typename RandomAccessIterator>
static inline
void bf6_insertion_outer(RandomAccessIterator dst,
                         std::size_t idx_begin,
                         std::size_t idx_end) {
  for (std::size_t idx = idx_begin + 1;
       idx < idx_end; idx++) {
    bf6_insertion_inner<RandomAccessIterator>(dst, idx, idx_begin);
  }
}

template <typename Key,
  typename Value,
  typename RandomAccessIterator>
  void bf6_helper_s(RandomAccessIterator dst,
                    const std::vector<std::pair<std::size_t, std::size_t>>& super_indexes,
                    int mask_bits,
                    int partition_bits) {
  std::tuple<std::size_t, Key, Value> tmp_bucket;
  int partitions, sqrt_partitions, iter, shift, new_mask_bits, idx_c;
  std::size_t h, mask, s_begin, s_end, idx_i, idx_j;

  partitions = 1 << partition_bits;
  sqrt_partitions = 1 << (partition_bits / 2);
  mask = (1ULL << mask_bits) - 1ULL;
  shift = mask_bits < partition_bits ? 0 : mask_bits - partition_bits;

  std::vector<std::size_t>counters(partitions);
  std::vector<std::pair<std::size_t, std::size_t>> indexes(partitions);

  for (int s = 0; s < partitions; s++) {
    s_begin = super_indexes[s].first;
    s_end = super_indexes[s].second;
    if (s_end - s_begin < 2)
      continue;
    // Partition too small, use insertion sort instead.
    if (s_end - s_begin < static_cast<std::size_t>(sqrt_partitions)) {
      bf6_insertion_outer<RandomAccessIterator>(dst, s_begin, s_end);
      continue;
    }
    // Setup counters for counting sort.
    for (int i = 0; i < partitions; i++)
      counters[i] = 0;
    indexes[0].first = s_begin;
    for (std::size_t i = s_begin; i < s_end; i++) {
      h = std::get<0>(dst[i]);
      counters[(h & mask) >> shift]++;
    }
    for (int i = 0; i < partitions - 1; i++) {
      indexes[i].second = indexes[i+1].first = indexes[i].first + counters[i];
    }
    indexes[partitions-1].second = indexes[partitions-1].first
      + counters[partitions-1];

    iter = 0;
    while (iter < partitions) {
      idx_i = indexes[iter].first;
      if (idx_i >= indexes[iter].second) {
        iter++;
        continue;
      }
      h = std::get<0>(dst[idx_i]);
      idx_c = static_cast<int>((h & mask) >> shift);
      if (idx_c == iter) {
        indexes[iter].first++;
        continue;
      }
      tmp_bucket = std::move(dst[idx_i]);
      do {
        h = std::get<0>(tmp_bucket);
        idx_c = static_cast<int>((h & mask) >> shift);
        idx_j = indexes[idx_c].first++;
        std::swap(dst[idx_j], tmp_bucket);
      } while (idx_j > idx_i);
    }

    new_mask_bits = mask_bits - partition_bits;
    if (new_mask_bits <= 0) {
      continue;
    }

    // Reset indexes
    indexes[0].first = s_begin;
    for (int i = 1; i < partitions; i++) {
      indexes[i].first = indexes[i-1].second;
    }
    bf6_helper_s<Key,Value,RandomAccessIterator>
      (dst, indexes, new_mask_bits, partition_bits);
  }
}

template <typename Key,
  typename Value,
  typename RandomAccessIterator>
  void bf6_helper_p(RandomAccessIterator dst,
                    const std::vector<std::pair<std::size_t, std::size_t>>& super_indexes,
                    int mask_bits,
                    int partition_bits,
                    std::atomic_int* super_counter) {
  std::tuple<std::size_t, Key, Value> tmp_bucket;
  std::size_t h, mask;
  int partitions, sqrt_partitions, s_idx, iter, idx_c;
  std::size_t idx_i, idx_j, s_begin, s_end;
  int shift;
  int new_mask_bits;

  partitions = 1 << partition_bits;
  sqrt_partitions = 1 << (partition_bits / 2);
  mask = (1ULL << mask_bits) - 1ULL;
  shift = mask_bits < partition_bits ? 0 : mask_bits - partition_bits;

  std::vector<std::size_t>counters(partitions);
  std::vector<std::pair<std::size_t, std::size_t>> indexes(partitions);

  s_idx = super_counter->fetch_add(1, std::memory_order_relaxed);

  while (s_idx < partitions) {
    s_begin = super_indexes[s_idx].first;
    s_end = super_indexes[s_idx].second;
    if (s_end - s_begin < 2) {
      s_idx = super_counter->fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    // Partition too small, use insertion sort instead.
    if (s_end - s_begin < static_cast<std::size_t>(sqrt_partitions)) {
      bf6_insertion_outer<RandomAccessIterator>(dst, s_begin, s_end);
      s_idx = super_counter->fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    // Setup counters for counting sort.
    for (int i = 0; i < partitions; i++)
      counters[i] = 0;
    indexes[0].first = s_begin;
    for (std::size_t i = s_begin; i < s_end; i++) {
      h = std::get<0>(dst[i]);
      counters[(h & mask) >> shift]++;
    }
    for (int i = 0; i < partitions - 1; i++) {
      indexes[i].second = indexes[i+1].first = indexes[i].first + counters[i];
    }
    indexes[partitions-1].second = indexes[partitions-1].first
      + counters[partitions-1];

    iter = 0;

    while (iter < partitions) {
      idx_i = indexes[iter].first;
      if (idx_i >= indexes[iter].second) {
        iter++;
        continue;
      }
      h = std::get<0>(dst[idx_i]);
      idx_c = static_cast<int>((h & mask) >> shift);
      if (idx_c == iter) {
        indexes[iter].first++;
        continue;
      }
      tmp_bucket = std::move(dst[idx_i]);
      do {
        h = std::get<0>(tmp_bucket);
        idx_c = static_cast<int>((h & mask) >> shift);
        idx_j = indexes[idx_c].first++;
        std::swap(dst[idx_j], tmp_bucket);
      } while (idx_j > idx_i);
    }

    new_mask_bits = mask_bits - partition_bits;
    if (new_mask_bits <= 0) {
      s_idx = super_counter->fetch_add(1, std::memory_order_relaxed);
      continue;
    }

    // Reset indexes
    indexes[0].first = s_begin;
    for (int i = 1; i < partitions; i++) {
      indexes[i].first = indexes[i-1].second;
    }
    bf6_helper_s<Key,Value,RandomAccessIterator>
      (dst, indexes, new_mask_bits, partition_bits);
    s_idx = super_counter->fetch_add(1, std::memory_order_relaxed);
  }
}

template<typename Key,
  typename Value,
  typename Hash,
  typename BidirectionalIterator,
  typename RandomAccessIterator>
  void radix_hash_bf6_worker(BidirectionalIterator begin,
                             BidirectionalIterator end,
                             RandomAccessIterator dst,
                             int thread_id,
                             int thread_num,
                             ThreadBarrier* barrier,
                             std::vector<std::size_t>* shared_counters,
                             std::vector<std::pair<std::size_t,std::size_t>>* indexes,
                             int partitions,
                             int shift) {
  std::size_t h;
  std::size_t dst_idx, tmp_cnt;

  // TODO maybe we can make no sort version in worker as well.
  for (auto iter = begin; iter != end; ++iter) {
    h = Hash{}(std::get<0>(*iter));
    (*shared_counters)[thread_id*partitions + (h>>shift)]++;
  }

  // in barrier
  if (barrier->wait()) {
    tmp_cnt = 0;
    for (int i = 0; i < partitions; i++) {
      for (int j = 0; j < thread_num; j++) {
        tmp_cnt += (*shared_counters)[j*partitions + i];
        (*shared_counters)[j*partitions + i] =
          tmp_cnt - (*shared_counters)[j*partitions + i];
      }
    }
    (*indexes)[0].first = 0;
    for (int i = 1; i < partitions; i++) {
      (*indexes)[i-1].second = (*indexes)[i].first = (*shared_counters)[i];
    }
    (*indexes)[partitions-1].second = tmp_cnt;

    barrier->wait();
  } else {
    barrier->wait();
  }

  for (auto iter = begin; iter != end; ++iter) {
    h = Hash{}(std::get<0>(*iter));
    dst_idx = (*shared_counters)[thread_id*partitions + (h>>shift)]++;
    std::get<0>(dst[dst_idx]) = h;
    std::get<1>(dst[dst_idx]) = iter->first;
    std::get<2>(dst[dst_idx]) = iter->second;
  }
}

// Features:
// * Use all bits to sort
// * worker do not use atomic (less memory sync)
template <typename Key,
  typename Value,
  typename Hash = std::hash<Key>,
  typename BidirectionalIterator,
  typename RandomAccessIterator>
  void radix_non_inplace_par(BidirectionalIterator begin,
                             BidirectionalIterator end,
                             RandomAccessIterator dst,
                             int num_threads,
                             int partition_bits) {
  int input_num, shift, partitions, thread_partition, new_mask_bits;
  std::atomic_int a_counter(0);
  ThreadBarrier barrier(num_threads);

  partitions = 1 << partition_bits;
  input_num = std::distance(begin, end);
  thread_partition = input_num / num_threads;

  shift = 64 - partition_bits;

  std::vector<std::size_t> shared_counters(partitions*num_threads);
  std::vector<std::pair<std::size_t, std::size_t>> indexes(partitions);
  std::vector<std::thread> threads(num_threads);

  for (int i = 0; i < num_threads-1; i++) {
    threads[i] = std::thread(radix_hash_bf6_worker<Key,Value,Hash,
                             BidirectionalIterator, RandomAccessIterator>,
                             begin + i * thread_partition,
                             begin + (i+1) * thread_partition,
                             dst, i, num_threads,
                             &barrier, &shared_counters, &indexes,
                             partitions, shift);
  }

  radix_hash_bf6_worker<Key,Value,Hash>(begin+(num_threads-1)*thread_partition,
                                        end, dst, num_threads-1, num_threads,
                                        &barrier, &shared_counters, &indexes,
                                        partitions, shift);
  for (int i = 0; i < num_threads-1; i++) {
    threads[i].join();
  }

  new_mask_bits = 64 - partition_bits;

  for (int i = 0; i < num_threads-1; i++) {
    threads[i] = std::thread(bf6_helper_p<Key,Value, RandomAccessIterator>,
                             dst, indexes, new_mask_bits,
                             partition_bits, &a_counter);
  }
  bf6_helper_p<Key,Value, RandomAccessIterator>(
      dst, indexes, new_mask_bits,
      partition_bits, &a_counter);
  for (int i = 0; i < num_threads-1; i++) {
    threads[i].join();
  }
}

template <typename Key,
  typename Value,
  typename Hash = std::hash<Key>,
  typename BidirectionalIterator,
  typename RandomAccessIterator>
  void radix_non_inplace_par(BidirectionalIterator begin,
                             BidirectionalIterator end,
                             RandomAccessIterator dst,
                             int num_threads) {
  std::size_t input_num;
  int partition_bits;
  input_num = std::distance(begin, end);
  partition_bits = optimal_partition(input_num);
  radix_non_inplace_par<Key,Value,Hash,BidirectionalIterator,RandomAccessIterator>
   (begin, end, dst, num_threads, partition_bits);
}

template <typename Key,
  typename Value,
  typename RandomAccessIterator>
  void radix_inplace_seq(RandomAccessIterator dst,
                         std::size_t input_num,
                         int partition_bits) {
  int shift, new_mask_bits;
  int iter, idx_c, partitions;
  std::size_t h, idx_i, idx_j;
  std::atomic_int a_counter(0);
  std::tuple<std::size_t, Key, Value> tmp_bucket;

  partitions = 1 << partition_bits;
  shift = 64 - partition_bits;

  std::size_t counters[partitions];
  std::vector<std::pair<std::size_t, std::size_t>> indexes(partitions);

  // Setup counters for counting sort.
  for (int i = 0; i < partitions; i++)
    counters[i] = 0;
  indexes[0].first = 0;
  for (std::size_t i = 0; i < input_num; i++) {
    h = std::get<0>(dst[i]);
    counters[h >> shift]++;
  }
  for (int i = 0; i < partitions - 1; i++) {
    indexes[i].second = indexes[i+1].first = indexes[i].first + counters[i];
  }
  indexes[partitions-1].second = indexes[partitions-1].first
  + counters[partitions-1];

  iter = 0;
  while (iter < partitions) {
    idx_i = indexes[iter].first;
    if (idx_i >= indexes[iter].second) {
      iter++;
      continue;
    }
    tmp_bucket = std::move(dst[idx_i]);
    do {
      h = std::get<0>(tmp_bucket);
      idx_c = static_cast<int>(h >> shift);
      idx_j = indexes[idx_c].first++;
      std::swap(dst[idx_j], tmp_bucket);
    } while (idx_j > idx_i);
  }

  // Reset indexes
  indexes[0].first = 0;
  for (int i = 1; i < partitions; i++) {
    indexes[i].first = indexes[i-1].second;
  }
  new_mask_bits = 64 - partition_bits;

  bf6_helper_p<Key,Value, RandomAccessIterator>(
      dst, indexes, new_mask_bits,
      partition_bits, &a_counter);
}

template <typename Key,
  typename Value,
  typename RandomAccessIterator>
  void radix_inplace_seq(RandomAccessIterator dst,
                         std::size_t input_num) {
  int partition_bits;
  partition_bits = optimal_partition(input_num);
  radix_inplace_seq<Key,Value,RandomAccessIterator>(dst, input_num, partition_bits);
}

template<typename Key,
  typename Value,
  typename RandomAccessIterator>
  void radix_hash_bf8_worker(RandomAccessIterator dst,
                             std::size_t begin,
                             std::size_t end,
                             int thread_id,
                             ThreadBarrier* barrier,
                             std::vector<std::mutex>* locks,
                             std::vector<std::atomic_size_t>* shared_counters,
                             std::vector<std::pair<std::size_t, std::size_t>>* sort_indexes,
                             std::vector<std::pair<std::size_t, std::size_t>>* indexes,
                             int partitions,
                             int shift) {
  std::size_t h;
  std::size_t local_counters[partitions];
  std::size_t idx_i, idx_j;
  int iter, idx_c;
  std::tuple<std::size_t, Key, Value> tmp_bucket;

  for (int i = 0; i < partitions; i++)
    local_counters[i] = 0;

  for (std::size_t i = begin; i < end; ++i) {
    h = std::get<0>(dst[i]);
    local_counters[h >> shift]++;
  }
  for (int i = 0; i < partitions; i++) {
    (*shared_counters)[i].fetch_add(local_counters[i],
                                    std::memory_order_relaxed);
  }

  // in barrier
  if (barrier->wait()) {
    (*indexes)[0].first = 0;
    (*sort_indexes)[0].first = (*sort_indexes)[0].second = 0;
    for (int i = 0; i < partitions - 1; i++) {
      (*indexes)[i].second =
       (*sort_indexes)[i+1].first =
       (*sort_indexes)[i+1].second = 
       (*indexes)[i+1].first = (*indexes)[i].first +
       (*shared_counters)[i].load(std::memory_order_relaxed);
    }
    (*indexes)[partitions-1].second = (*indexes)[partitions-1].first
      + (*shared_counters)[partitions-1].load(std::memory_order_relaxed);
    barrier->wait();
  } else {
    barrier->wait();
  }

  // scatter threads in partitions
  iter = (thread_id*17) % partitions;
  while (iter < partitions) {
    (*locks)[iter].lock();
   retry_no_lock:
    if ((*sort_indexes)[iter].first >= (*indexes)[iter].second) {
      (*locks)[iter].unlock();
      iter++;
      continue;
    }
    idx_i = (*sort_indexes)[iter].first++;

    h = std::get<0>(dst[idx_i]);
    idx_c = static_cast<int>(h >> shift);
    if (idx_c == iter) {
      idx_j = (*sort_indexes)[iter].second++;
      if (idx_i != idx_j) {
        std::swap(dst[idx_i], dst[idx_j]);
      }
      goto retry_no_lock;
    }

    tmp_bucket = std::move(dst[idx_i]);
    (*locks)[iter].unlock();
    while (true) {
      h = std::get<0>(tmp_bucket);
      idx_c = static_cast<int>(h >> shift);
      (*locks)[idx_c].lock();
      if ((*sort_indexes)[idx_c].first > (*sort_indexes)[idx_c].second) {
        // We have a blank spot to fill tmp_bucket in
        idx_j = (*sort_indexes)[idx_c].second++;
        dst[idx_j] = std::move(tmp_bucket);
        (*locks)[idx_c].unlock();
        break;
      }
      idx_j = (*sort_indexes)[idx_c].first;
      (*sort_indexes)[idx_c].first++;
      (*sort_indexes)[idx_c].second++;
      std::swap(tmp_bucket, dst[idx_j]);
      (*locks)[idx_c].unlock();
    }
  }
}

template <typename RandomAccessIterator>
void radix_inplace_par(RandomAccessIterator dst,
                       std::size_t input_num,
                       int num_threads,
                       int partition_bits) {
  typedef typename std::tuple_element<1,
    typename RandomAccessIterator::value_type>::type Key;
  typedef typename std::tuple_element<2,
    typename RandomAccessIterator::value_type>::type Value;

  int shift, partitions, thread_partition, new_mask_bits;
  std::atomic_int a_counter(0);
  ThreadBarrier barrier(num_threads);

  partitions = 1 << partition_bits;
  thread_partition = input_num / num_threads;

  shift = 64 - partition_bits;

  std::vector<std::atomic_size_t> shared_counters(partitions);
  std::vector<std::mutex> locks(partitions);
  std::vector<std::pair<std::size_t, std::size_t>> indexes(partitions);
  std::vector<std::pair<std::size_t, std::size_t>> sort_indexes(partitions);
  std::vector<std::thread> threads(num_threads);

  for (int i = 0; i < num_threads-1; i++) {
    threads[i] = std::thread(radix_hash_bf8_worker<Key,Value,
                             RandomAccessIterator>,
                             dst, i * thread_partition,
                             (i+1) * thread_partition,
                             i, &barrier, &locks, &shared_counters,
                             &sort_indexes, &indexes,
                             partitions, shift);
  }

  radix_hash_bf8_worker<Key,Value>(dst, (num_threads-1)*thread_partition,
                                   input_num, num_threads-1, &barrier, &locks,
                                   &shared_counters, &sort_indexes, &indexes,
                                   partitions, shift);
  for (int i = 0; i < num_threads-1; i++) {
    threads[i].join();
  }

  new_mask_bits = 64 - partition_bits;

  for (int i = 0; i < num_threads-1; i++) {
    threads[i] = std::thread(bf6_helper_p<Key,Value, RandomAccessIterator>,
                             dst, indexes, new_mask_bits,
                             partition_bits, &a_counter);
  }
  bf6_helper_p<Key,Value, RandomAccessIterator>(
      dst, indexes, new_mask_bits,
      partition_bits, &a_counter);
  for (int i = 0; i < num_threads-1; i++) {
    threads[i].join();
  }
}

template <typename RandomAccessIterator>
void radix_inplace_par(RandomAccessIterator dst,
                       std::size_t input_num,
                       int num_threads) {
  int partition_bits;
  partition_bits = optimal_partition(input_num);
  radix_inplace_par<RandomAccessIterator>(dst, input_num, num_threads, partition_bits);
}
}
#endif
