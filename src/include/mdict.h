/*
 * Copyright (c) 2025-Present
 * All rights reserved.
 *
 * This code is licensed under the BSD 3-Clause License.
 * See the LICENSE file for details.
 */

#include <utility>

#pragma once

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>  // std::stof
#include <vector>

#include "mdict_extern.h"
#include "ripemd128.h"

/**
 * mdx struct analysis
 * mdx file:
 *#| dictionary header
 *    | [0:4]: the length of dictionary header (header_bytes_size)
 *      | uint32 integer
 *    | [4:header_bytes_size + 4]: dictionary header info bytes
 *(header_info_bytes)
 *      | header info bytes, little-endian utf16 bytes, xml format
 *    | [header_bytes_size + 4: header_bytes_size + 8]: dictionary header info
 *adler32 checksum (alder32_checksum)
 *      | assert(zlib.alder32(header_bytes) & 0xffffffff, alder32_checksum)
 *#| key blocks (starts with header_bytes_size + 8, key block offset)
 *    | note: num bytes format, if version >= 2.0, number format is uint64
 *(8bytes), otherwise, number format is uint32
 *    | note: encrypt == 1 && passcode != nil, TODO
 *   #| key block header:
 *      | for version >= 2.0
 *      | [0:8]    - number of key blocks (key_block_num)
 *      | [8:16]   - number of entries (entries_num)
 *      | [16:24]  - key block info decompressed size (if version < 2.0, this
 *part does not exist)
 *      | [24:32]  - key block info size (key_block_info_size)
 *      | [32:40]  - key block size (key_block_size)
 *      | [40:44]  - key info (key block header) alder32 checksum *
 *                  (*: this part only include ver>2.0, and not include in key
 *block info length)
 *      | for version < 2.0
 *      | [0:4]    - number of key blocks
 *      | [4:8]    - number of entries
 *      | [8:12]   - key block info size
 *      | [12:16]  - key block size*
 *      | summary: if version >= 2.0, the key block header buffer size is 5 * 8
 *= 40 bytes, and actually, with 4 bytes checksum,
 *      |          so, if version >= 2.0, key block header length = 44 bytes
 *      |          else if version < 2.0, the key block header buffer size is 4
 ** 4 = 16 bytes
 *      |
 *   #| key block info
 *      | note: offset, if version >= 2.0, start with header_bytes_size + 8 +
 *(44 = key_block_header_length),
 *      |               else, version < 2.0, start with header_bytes_size + 8 +
 *(16 = key_block_header_length)
 *      | key block info size equals the number read from key block header
 *(key_block_size)
 *      | key_block_info_buffer = dict_file.read(header_bytes_size + 8 +
 *key_block_header_length, key_block_info_size);
 *      | typedef: key_block_info_list [{
 *      |    compressed_size
 *      |    decompressed_size
 *      | },...]
 *      | key_block_info_list = decode_key_block_info(key_block_info_buffer)
 *          | deceode_key_block_info:
 *
 *      | assert(key_block_info_list.length, key_block_num);
 *      | key_block_compressed = dict_file.read(header_bytes_size + 8 +
 *key_block_header_length + key_block_info_size, key_block_size)
 *      | key_list = decode_key_block(key_block_compressed, key_block_info_list)
 *      | note: record_block_offset = header_bytes_size + 8 +
 *key_block_header_length + key_block_info_size + key_block_size
 *
 *#| record block:
 *   #| record block header (recoder_block_header)
 *    | if version >= 2.0:
 *      | [0:8]   - record block number (record_block_num)
 *      | [8:16]  - number of the key-value entries (entries_num)
 *      | [16:24] - record block info size (record_block_info_size)
 *      | [24:32] - record block size (record_block_size)
 *    | else if version < 2.0:
 *      | [0:4]   - record block number (record_block_num)
 *      | [4:8]  - number of the key-value entries (entries_num)
 *      | [8:12] - record block info size (record_block_info_size)
 *      | [12:16] - record block size (record_block_size)
 *    | note: every record block contains a lot of entries
 *    | typedef: record_block_info_list [{
 *    |   compressed_size,
 *    |   decompressed_size
 *    | },...]
 *  #| record block info
 *    | decode record_block_info_list
 *      | for i =0; i < record_block_num; ++i
 *      |    compressed_size = readnumber(dict_file.readbyte(number_width)) //
 *number_width = 8 (ver >= 2.0), 4(ver < 2.0)
 *      |    decompresed_size = readnumber(dict_file.readbyte(number_width))
 *      |    size_counter += 2 * number_width
 *      | assert(size_counter, record_block_info_size)
 *  #| record block starts at: record_block_offset + record_block_header_size +
 *record_block_info_size
 *    | decode record block
 *      | for i = 0; i < record_block_info_list.length(record_block_num); ++i
 *      |   compressed_size = record_block_info_list[i].compressed_size
 *      |   decompressed_size = record_block_info_list[i].decompressed_size
 *      |   #record_block_compressed = dict_file.readbytes(compressed_size)
 *      |     | decode and decrypt record_block_compressed, and gets block's
 *keys list
 *      |
 *
 *
 */

namespace mdict {

#define ENCRYPT_NO_ENC 0
#define ENCRYPT_RECORD_ENC 1
#define ENCRYPT_KEY_INFO_ENC 2

#define NUMFMT_BE_8BYTESQ 0;
#define NUMFMT_BE_4BYTESI 1;

#define ENCODING_UTF8 0;
#define ENCODING_UTF16 1;
#define ENCODING_BIG5 2;
#define ENCODING_GBK 3;
#define ENCODING_GB2312 4;
#define ENCODING_GB18030 5;

#define MDXTYPE "MDX";
#define MDDTYPE "MDD";

/**
 * key block info class definition
 */
class key_block_info {
 public:
  // first key of this key block
  std::string first_key;
  // last key of this key block
  std::string last_key;
  // key block start offset
  unsigned long key_block_start_offset;
  // key block compressed size
  unsigned long key_block_comp_size;
  unsigned long key_block_comp_accumulator;
  // key block decompressed size
  unsigned long key_block_decomp_size;
  unsigned long key_block_decomp_accumulator;

  /**
   * constructor
   * @param first_key first key of this key block
   * @param last_key  last key of this key block
   * @param kb_start_ofset key block start offset
   * @param kb_comp_size  key block compress size
   * @param kb_decomp_size key block decompressed size
   */
  key_block_info(std::string first_key, std::string last_key,
                 unsigned long kb_start_ofset, unsigned long kb_comp_size,
                 unsigned long kb_decomp_size, unsigned long kb_comp_accu,
                 unsigned long kb_decomp_accu) {
    this->key_block_comp_size = kb_comp_size;
    this->key_block_decomp_size = kb_decomp_size;
    this->key_block_start_offset = kb_start_ofset;
    this->first_key = first_key;
    this->last_key = last_key;
    this->key_block_comp_accumulator = kb_comp_accu;
    this->key_block_decomp_accumulator = kb_decomp_accu;
  }
};

class key_list_item {
 public:
  unsigned long record_start;
  std::string key_word;
  key_list_item(unsigned long kid, std::string kw)
      : record_start(kid), key_word(std::move(kw)) {}
};

class record_header_item {
 public:
  unsigned long block_id;
  unsigned long compressed_size;
  unsigned long decompressed_size;
  unsigned long compressed_size_accumulator;
  unsigned long decompressed_size_accumulator;
  record_header_item(unsigned long bid, unsigned long comp_size,
                     unsigned long uncomp_size, unsigned long comp_accu,
                     unsigned long decomp_accu)
      : block_id(bid),
        compressed_size(comp_size),
        decompressed_size(uncomp_size),
        compressed_size_accumulator(comp_accu),
        decompressed_size_accumulator(decomp_accu) {};
};

class record {
 public:
  std::string key_text;
  unsigned long key_idx;
  int encoding;
  unsigned long record_start_offset;
  unsigned int comp_size;
  unsigned int uncomp_size;
  unsigned int comp_type;
  bool record_encrypted;
  unsigned long relative_record_start;
  unsigned long relative_record_end;
  record(std::string ktext, unsigned long kidx, int encoding,
         unsigned long r_start_ofset, unsigned int csize, unsigned int uncsize,
         unsigned int comp_type, bool renc, unsigned long rela_stat,
         unsigned long rela_end) {
    this->key_text = ktext;
    this->key_idx = kidx;
    this->encoding = encoding;
    this->record_start_offset = r_start_ofset;
    this->comp_size = csize;
    this->uncomp_size = uncsize;
    this->comp_type = comp_type;
    this->record_encrypted = renc;
    this->relative_record_start = rela_stat;
    this->relative_record_end = rela_end;
  }
};

/**
 * Mdict class definition
 */
class Mdict {
 public:
  /**
   * constructor
   * @param fn dictionary file name
   */
  Mdict(std::string fn) noexcept;

  /**
   * constructor with additional files
   * @param fn dictionary file name
   * @param aff_fn affix file name
   * @param dic_fn dictionary file name
   */
  Mdict(std::string fn, std::string aff_fn, std::string dic_fn) noexcept;

  /**
   * deconstructor
   */
  ~Mdict();

  /**
   * lookup the definition of a word
   * @param word the word wich we want to search
   * @return
   */
  std::string lookup(std::string word);

  /**
   * lookup the definition of a word by system search finction from all keys list
   * @param word the word wich we want to search
   * @return
   */
  std::string lookup0(std::string word);

  /**
   * Locate a resource in the dictionary
   * @param resource_name The name of the resource to locate
   * @param encoding The encoding type for the result (MDICT_ENCODING_BASE64 or
   * MDICT_ENCODING_HEX)
   * @return The located resource content in the specified encoding
   */
  std::string locate(const std::string resource_name,
                     mdict_encoding_t encoding = MDICT_ENCODING_BASE64);

  /**
   * suggest simuler word which matches the prefix
   * @param word the word's prefix
   * @param prefix_len the word's length (optional)
   * @return
   */
  std::vector<std::string> suggest(const std::string word);

  /**
   *
   * @param word
   * @return
   */
  std::vector<std::string> stem(const std::string word);

  /**
   * Check if a word exists in the dictionary
   * @param word The word to check
   * @param word_len Length of the word
   * @return true if the word exists, false otherwise
   */
  bool contains(char *word, int word_len);

  /**
   * Initialize the dictionary by reading its header and block information
   */
  void init();

  /**
   * Reduce search range for a phrase
   * @param phrase The phrase to search for
   * @param start Starting position in the dictionary
   * @param end Ending position in the dictionary
   * @return The reduced range
   */
  long reduce_key_info_block(std::string phrase, unsigned long start, unsigned long end);

  /**
   * Reduce search range using a word list
   * @param wordlist List of words to search for
   * @param phrase The phrase to search for
   * @return The reduced range
   */
  long reduce_key_info_block_items_vector(std::vector<key_list_item *> wordlist, std::string phrase);

  /**
   * Reduce search range from a record start position
   * @param record_start Starting position of the record
   * @return The reduced range
   */
  long reduce_record_block_offset(unsigned long record_start);

  /**
   *  search definiation from key_text:def pair vector
   * @param vec  key:def pair vector
   * @param phrase the target word phrase
   * @return definition
   */
  std::string reduce_particial_keys_vector(std::vector<std::pair<std::string, std::string>>& vec,
                      std::string phrase);

  std::vector<key_list_item *> keyList();

  std::string parse_definition(const std::string word,
                               unsigned long record_start);

  std::string filetype;

  /**
   * Read data from the dictionary file
   * @param offset Starting offset in the file
   * @param len Number of bytes to read
   * @param buf Buffer to store the read data
   */
  void readfile(uint64_t offset, uint64_t len, char *buf);

  /**
   * Read and parse the dictionary header
   */
  void read_header();

  /**
   * Read the key block header
   */
  void read_key_block_header();

  /**
   * Read the key block information
   */
  void read_key_block_info();

  /**
   * Decode key block information from a buffer
   * @param key_block_info_buffer Buffer containing key block information
   * @param kb_info_buff_len Length of the buffer
   * @param key_block_num Number of key blocks
   * @param entries_num Number of entries
   * @return 0 on success, non-zero on failure
   */
  int decode_key_block_info(char *key_block_info_buffer,
                            unsigned long kb_info_buff_len, int key_block_num,
                            int entries_num);

  /**
   * Decode a key block from a buffer
   * @param key_block_buffer Buffer containing the key block
   * @param kb_buff_len Length of the buffer
   * @return 0 on success, non-zero on failure
   */
  int decode_key_block(unsigned char *key_block_buffer,
                       unsigned long kb_buff_len);

  std::vector<key_list_item *> decode_key_block_by_block_id(
      unsigned long block_id);

  /**
   * Read the record block header
   * @return 0 on success, non-zero on failure
   */
  int read_record_block_header();

  /**
   * Decode the record block
   * @return 0 on success, non-zero on failure
   */
  int decode_record_block();

  std::vector<std::pair<std::string, std::string>> decode_record_block_by_rid(
      unsigned long rid /* record id */);

  /**
   * Print the dictionary header information
   */
  void printhead() {
    // std::cout << "version: " << this->version << std::endl
    //           << "header_bytes_size: " << this->header_bytes_size <<
    //           std::endl
    //           << "encoding: " << this->encoding << std::endl
    //           << "key_block_num: " << this->key_block_num << std::endl
    //           << "entries_num: " << this->entries_num << std::endl
    //           << "key_block_info_decompress_size: "
    //           << this->key_block_info_decompress_size << std::endl
    //           << "key_block_info_size: " << this->key_block_info_size
    //           << std::endl
    //           << "key_block_size: " << this->key_block_size << std::endl;
  }

  /**
   * Check if a string ends with a specific suffix
   * @param fullString The string to check
   * @param ending The suffix to look for
   * @return true if the string ends with the suffix, false otherwise
   */
  bool endsWith(const std::string &fullString, const std::string &ending);

 private:
  /********************************
   *     general section           *
   ********************************/
  // dictionary file name
  const std::string filename;

  // file input stream
  std::ifstream instream;

  /********************************
   *     header section           *
   ********************************/
  // ---------------------
  //     header part
  // ---------------------

  std::string header_buffer;

  // offset part (important)
  // dictionary header part
  // | dictionary header
  //    | [0:4]: the length of dictionary header (header_bytes_size)
  //      | uint32 integer
  //    | [4:header_bytes_size + 4]: dictionary header info bytes
  //    (header_info_bytes)
  //      | header info bytes, little-endian utf16 bytes, xml format
  //    | [header_bytes_size + 4: header_bytes_size + 8]: dictionary header info
  //    adler32 checksum (alder32_checksum)
  //      | assert(zlib.alder32(header_bytes) & 0xffffffff, alder32_checksum)
  uint32_t header_bytes_size = 0;

  // key block start offset
  // key_block_start_offset = header_bytes_size + 8;
  uint32_t key_block_start_offset = 0;

  // key_block_info_start_offset = key_block_start_offset + info_size (>=2.0:
  // 40+4, <2.0: 16)
  uint32_t key_block_info_start_offset = 0;
  // key block compressed start offset = this->key_block_info_start_offset +
  // key_block_info_size
  uint32_t key_block_compressed_start_offset = 0;

  // ---------------------
  //     block key info part
  // ---------------------
  uint64_t key_block_num = 0;
  uint64_t entries_num = 0;
  uint64_t key_block_info_decompress_size = 0;
  uint64_t key_block_info_size = 0;
  uint64_t key_block_size = 0;

  // ---------------------
  //  key block body offset
  // ---------------------

  uint64_t key_block_body_start = 0;

  // head info part
  int encrypt = 0;
  float version = 1.0;

  int number_width = 8;
  int number_format = 0;

  int encoding = ENCODING_UTF8;

  // key block info list
  std::vector<key_block_info *> key_block_info_list;

  // key list (key word list)
  std::vector<key_list_item *> key_list;

  // -------------------
  // record block section
  // -------------------

  uint64_t record_block_info_offset;

  uint64_t record_block_info_size;  // >= 2.0 32, else 16

  uint64_t record_block_number;          // [0:8/4]    - record blcok number
  uint64_t record_block_entries_number;  // [8:16/4:8] - num entries the
                                         // key-value entries number
  uint64_t record_block_header_size;  // [16:24/8:12] - record block info size
                                      // // TODO
  uint64_t record_block_size;         // [24:32/12:16] - record block size

  std::vector<record_header_item *> record_header;

  // record_block_offset = record_block_info_offset + record_info_size +
  // record_header_size
  uint64_t record_block_offset;

  std::vector<record *> key_data;

  /**
   * split key block from key block buffer
   * @param key_block the key block buffer
   * @param key_block_len the key block buffer length
   */
  // # void split_key_block(unsigned char *key_block, unsigned long
  //  key_block_len);
  std::vector<key_list_item *> split_key_block(unsigned char *key_block,
                                               unsigned long key_block_len,
                                               unsigned long block_id);

  /********************************
   *     INNER DICTIONARY PART    *
   ********************************/

  /********************************
   *     header section           *
   ********************************/

  /********************************
   *     key block info section   *
   ********************************/

  /********************************
   *     record block section     *
   ********************************/
};
}  // namespace mdict
