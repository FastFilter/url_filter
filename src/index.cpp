#include "performancecounters/benchmarker.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <stdlib.h>
#include <vector>

extern "C" {
#include "binaryfusefilter.h"
}
std::string random_string() {
  auto randchar = []() -> char {
    const char charset[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[rand() % max_index];
  };
  size_t desired_length = rand() % 128;
  std::string str(desired_length, 0);
  std::generate_n(str.begin(), desired_length, randchar);
  return str;
}

void pretty_print(size_t volume, size_t bytes, std::string name,
                  event_aggregate agg) {
  printf("%-30s : ", name.c_str());
  printf(" %5.2f GB/s ", bytes / agg.fastest_elapsed_ns());
  printf(" %5.1f Ma/s ", volume * 1000.0 / agg.fastest_elapsed_ns());
  printf(" %5.2f ns/d ", agg.fastest_elapsed_ns() / volume);
  if (collector.has_events()) {
    printf(" %5.2f GHz ", agg.fastest_cycles() / agg.fastest_elapsed_ns());
    printf(" %5.2f c/d ", agg.fastest_cycles() / volume);
    printf(" %5.2f i/d ", agg.fastest_instructions() / volume);
    printf(" %5.1f c/b ", agg.fastest_cycles() / bytes);
    printf(" %5.2f i/b ", agg.fastest_instructions() / bytes);
    printf(" %5.2f i/c ", agg.fastest_instructions() / agg.fastest_cycles());
  }
  printf("\n");
}

uint64_t simple_hash(const std::string &line) {
  uint64_t h = 0;
  for (unsigned char c : line) {
    h = (h * 177) + c;
  }
  h ^= line.size();
  return h;
}

int main(int argc, char **argv) {
  std::vector<std::string> inputs;

  if (argc == 1) {
    printf("You must pass a list of URLs (one per line). For instance:\n");
    printf("./benchmark data/top-1m.csv  \n");
    return EXIT_FAILURE;
  } else {
    std::ifstream input(argv[1]);
    if (!input) {
      std::cerr << "Could not open " << argv[1] << std::endl;
      exit(EXIT_FAILURE);
    }
    size_t volume = 0;
    for (std::string line; std::getline(input, line);) {
      std::string ref = line;
      ref.erase(std::find_if(ref.rbegin(), ref.rend(),
                             [](unsigned char ch) { return !std::isspace(ch); })
                    .base(),
                ref.end());
      volume += ref.size();
      inputs.push_back(ref);
    }
    std::cout << "loaded " << inputs.size() << " names" << std::endl;
    std::cout << "average length " << double(volume) / inputs.size()
              << " bytes/name" << std::endl;
  }
  printf("\n");
  /* We are going to check for duplicates. If you have too many duplicates,
   * something might be wrong. */

  std::sort(inputs.begin(), inputs.end());
  auto dup_str = std::adjacent_find(inputs.begin(), inputs.end());
  while (dup_str != inputs.end()) {
    std::cout << "duplicated string " << *dup_str << std::endl;
    dup_str = std::adjacent_find(dup_str + 1, inputs.end());
  }
  size_t bytes = 0;
  for (const std::string &s : inputs) {
    bytes += s.size();
  }
  printf("total volume %zu bytes\n", bytes);
  /* We are going to test our hash function to make sure that it is sane. */

  // hashes is *temporary* and does not count in the memory budget
  std::vector<uint64_t> hashes(inputs.size());
  for (size_t i = 0; i < inputs.size(); i++) {
    hashes[i] = simple_hash(inputs[i]);
  }
  std::sort(hashes.begin(), hashes.end());
  auto dup = std::adjacent_find(hashes.begin(), hashes.end());
  size_t count = 0;
  while (dup != hashes.end()) {
    count++;
    dup = std::adjacent_find(dup + 1, hashes.end());
  }
  printf("number of duplicates hashes %zu\n", count);
  printf("ratio of duplicates  hashes %f\n", count / double(hashes.size()));

  size_t size = hashes.size();
  /*******************************
   * Let us benchmark the filter!
   ******************************/
  /**
   * A filter is a simple data structure that can be easily serialized (e.g., to
   * disk). https://github.com/FastFilter/xor_singleheader#persistent-usage
   */
  binary_fuse16_t filter;
  // Memory allocation (trivial):
  bool is_ok = binary_fuse16_allocate(size, &filter);
  if (!is_ok) {
    printf("You probably ran out of memory. Try a smaller size.\n");
    return EXIT_FAILURE;
  }
  // Construction:
  is_ok = binary_fuse16_populate(hashes.data(), size, &filter);
  if (!is_ok) {
    // This cannot happen unless there is a bug in the library or you provided a
    // bad input (e.g., all duplicates).
    printf("Construction failed. This should not happen.\n");
    return EXIT_FAILURE;
  }
  // Let us check the size of the filter in bytes:
  size_t filter_volume = binary_fuse16_size_in_bytes(&filter);
  printf("\nfilter memory usage : %zu bytes (%.1f %% of input)\n",
         filter_volume, 100.0 * filter_volume / bytes);
  printf("\nfilter memory usage : %1.f bits/entry\n",
         8.0 * filter_volume / hashes.size());
  printf("\n");
  // Let us test the query with bogus strings
  std::vector<std::string> query_set_bogus;
  size_t bogus_volume = 0;
  for (size_t i = 0; i < 100000; i++) {
    query_set_bogus.push_back(random_string());
  }

  size_t fpp = 0;
  for (const std::string &ref : query_set_bogus) {
    bogus_volume += ref.size();
    bool in_set = binary_fuse16_contain(simple_hash(ref), &filter);
    if (in_set) {
      fpp++;
    }
  }
  printf("false-positive rate %f\n", fpp / double(query_set_bogus.size()));
  volatile size_t basic_count = 0;
  printf("Benchmarking queries:\n");

  pretty_print(query_set_bogus.size(), bogus_volume, "binary_fuse16_contain",
               bench([&query_set_bogus, &filter, &basic_count]() {
                 for (std::string &ref : query_set_bogus) {
                   basic_count +=
                       binary_fuse16_contain(simple_hash(ref), &filter);
                 }
               }));

  /* Let's check for duplicated hashes */
  if (argc > 2) {
    std::ifstream input(argv[2]);
    uint32_t num_filter_positives = 0, num_strings = 0, matches = 0;

    if (!input) {
      std::cerr << "Could not open " << argv[2] << std::endl;
      exit(EXIT_FAILURE);
    }

    for (std::string line; std::getline(input, line);) {
      std::string ref = line;

      ref.erase(std::find_if(ref.rbegin(), ref.rend(),
                             [](unsigned char ch) { return !std::isspace(ch); })
                    .base(),
                ref.end());

      num_strings++;
      bool is_match = std::binary_search(inputs.begin(), inputs.end(), ref);
      if (is_match) {
        matches++;
      }

      if (binary_fuse16_contain(simple_hash(ref), &filter) && !is_match) {
        num_filter_positives++;
      }
    }

    printf("\nnumber of false positives %u [%.4f%%] out of %u\n",
           num_filter_positives,
           ((float)num_filter_positives * 100) / (float)num_strings,
           num_strings);
    printf("\nnumber of actual positives %u [%.4f%%] out of %u\n", matches,
           ((float)matches * 100) / (float)num_strings, num_strings);
  } else {
    printf("\nno secondary file with strings provided\n");
  }

  printf("\nBenchmarking construction speed\n");

  pretty_print(inputs.size(), bytes, "binary_fuse16_populate",
               bench([&hashes, &filter, &size]() {
                 binary_fuse16_populate(hashes.data(), size, &filter);
               }));
  binary_fuse16_free(&filter);

  return EXIT_SUCCESS;
}