#include <string>
#include <iostream>
#include <vector>
#include <set>
#include <list>
#include <parallel_hashmap/phmap.h>
#include <cstdint>
#include "HashUtils/hashutil.hpp"
#include "HashUtils/aaHasher.hpp"
#include <zlib.h>
#include <cstdio>
#include <kseq/kseq.h>
#include <fstream>
#include <sstream>

KSEQ_INIT(gzFile, gzread)

using phmap::flat_hash_map;


struct kmer_row {
    std::string str;
    uint64_t hash;
};

/* 
--------------------------------------------------------
                        InputModule:Parent
--------------------------------------------------------
*/

class kmerDecoder {

protected:
    unsigned int chunk_size{};

    flat_hash_map<std::string, std::vector<kmer_row>> kmers;
    std::string fileName;
    gzFile fp{};
    kseq_t *kseqObj{};


    void initialize_kSeq();

    bool FILE_END = false;

    virtual void extractKmers() = 0;


    // Mode 0: Murmar Hashing | Irreversible
    // Mode 1: Integer Hashing | Reversible | Full Hashing
    // Mode 2: TwoBitsHashing | Not considered hashing, just store the two bits representation


public:

    flat_hash_map<std::string, std::vector<kmer_row>> *getKmers();

    Hasher * hasher{};

    int hash_mode = 0;
    bool canonical = true;
    std::string slicing_mode;

    virtual void seq_to_kmers(std::string &seq, std::vector<kmer_row> &kmers) = 0;

    virtual int get_kSize() = 0;

    bool end() const;

    void next_chunk();

    std::string get_filename();

    virtual void setHashingMode(int hash_mode, bool canonical) = 0;

    // hash single kmer
    uint64_t hash_kmer(const std::string & kmer_str) {
        return this->hasher->hash(kmer_str);
    }


    // Inverse hash single kmer
    std::string ihash_kmer(uint64_t kmer_hash) {
        return this->hasher->Ihash(kmer_hash);
    }

    static kmerDecoder *initialize_hasher(int kmer_size, int hash_mode = 1);


    static Hasher *create_hasher(int kmer_size, int hash_mode = 1) {

        switch (hash_mode) {
            case 0:
                return (new MumurHasher(2038074761));
            case 1:
                return (new IntegerHasher(kmer_size));
            case 2:
                return (new TwoBitsHasher(kmer_size));
            case 3:
                return(new bigKmerHasher(kmer_size)); // kmer size here is useless
            default:
                std::cerr << "Hashing mode : " << hash_mode << ", is not supported \n";
                std::cerr << "Mode 0: Murmar Hashing | Irreversible\n"
                             "Mode 1: Integer Hashing | Reversible\n"
                             "Mode 2: TwoBitsHashing | Not considered hashing, just store the two bits representation\n"
                             "Mode 3: bigKmerHasher | Irreversible hashing using std::hash<T> (Supported only for Kmers mode)\n"
                          <<
                          "Default: Integer Hashing" << std::endl;
                exit(1);
        }

    }

    virtual ~kmerDecoder(){
        delete this->hasher;
        kseq_destroy(this->kseqObj);
        gzclose(this->fp);
        this->kmers.clear();
    }

};

/* 
--------------------------------------------------------
                        Custom Items (new..)
--------------------------------------------------------
*/




class CSVRow
{
    public:
        std::string const& operator[](std::size_t index) const
        {
            return m_data[index];
        }
        std::size_t size() const
        {
            return m_data.size();
        }
        void readNextRow(std::istream& str)
        {
            std::string         line;
            std::getline(str, line);

            std::stringstream   lineStream(line);
            std::string         cell;

            m_data.clear();
            while(std::getline(lineStream, cell, '\t'))
            {
                m_data.push_back(cell);
            }
            // This checks for a trailing comma with no data after it.
            if (!lineStream && cell.empty())
            {
                // If there was a trailing comma then add an empty element.
                m_data.push_back("");
            }
        }
    private:
        std::vector<std::string>    m_data;
};



class Items : public kmerDecoder {

private:

    void extractKmers();

    std::hash<std::string> child_hasher;
    flat_hash_map<uint64_t, std::string> hash_to_str;
    flat_hash_map<std::string, std::vector<std::string>> parent_to_children;
    std::string slicing_mode = "items";

    std::string filename;
    bool END = false;

public:
    int kSize = 32;
    CSVRow row;
    ifstream file;

    void setHashingMode(int hash_mode, bool canonical = true);
    void seq_to_kmers(std::string &seq, std::vector<kmer_row> &kmers);

    Items() {

    }

    Items(const std::string &filename);


    int get_kSize() {
        return this->kSize;
    }


    // hash single item
    uint64_t hash_kmer(const std::string &item_str) {
        return this->child_hasher(item_str);
    }

    // Inverse hash single item
    std::string ihash_kmer(const uint64_t &item_hash) {
        return this->hash_to_str[item_hash];
    }

    bool end();

    void next_chunk();


};


/* 
--------------------------------------------------------
                        Default Kmers
--------------------------------------------------------
*/


class Kmers : public kmerDecoder {

private:
    unsigned kSize{};

    void extractKmers() override;

public:

    explicit Kmers(int k_size, int hash_mode = 1) : kSize(k_size) {
        this->hasher = new IntegerHasher(kSize);
        this->slicing_mode = "kmers";
        this->hash_mode = 1;
        this->canonical = true;
        if (hash_mode != 1) {
            Kmers::setHashingMode(hash_mode);
        }
    };

    Kmers(const std::string &filename, unsigned int chunk_size, int kSize) {
        this->kSize = kSize;
        this->fileName = filename;
        this->chunk_size = chunk_size;
        this->initialize_kSeq();
        this->hasher = new IntegerHasher(kSize);
        this->hash_mode = 1;
        this->canonical = true;
        this->slicing_mode = "kmers";
    }

    void setHashingMode(int hash_mode, bool canonical = true) {

        // bool canonical is used only in IntegerHasher and TwoBitsHasher

        this->hash_mode = hash_mode;
        this->canonical = canonical;

        if (hash_mode == 0) hasher = (new MumurHasher(2038074761));
        else if (hash_mode == 1) {
            if (canonical) hasher = (new IntegerHasher(kSize));
            else hasher = (new noncanonical_IntegerHasher(kSize));
        } else if (hash_mode == 2) {
            if (canonical) {
                hasher = (new TwoBitsHasher(kSize));
            } else {
                hasher = (new noncanonical_TwoBitsHasher(kSize));
            }
        } else if (hash_mode == 3){
            hasher = (new bigKmerHasher(kSize));
        }else {
            hasher = (new IntegerHasher(kSize));
        }

    }


    void seq_to_kmers(std::string &seq, std::vector<kmer_row> &kmers) override;


    int get_kSize() {
        return this->kSize;
    }

    ~Kmers() override{}

};


/* 
--------------------------------------------------------
                        Skipmers
--------------------------------------------------------
*/

class Skipmers : public kmerDecoder {
private:
    int m, n, k, S;
    std::vector<int> ORFs = {0, 1, 2};

    void extractKmers();

public:

    Skipmers(uint8_t m, uint8_t n, uint8_t k, int ORF = 0) {
        if (n < 1 || n < m || k < m || k % m != 0) {
            std::cerr << "Error: invalid skip-mer shape!"
                      << "Conditions: 0 < m <= n < k & k must be multiple of m" << std::endl;

            exit(1);
        }

        if (ORF) {
            this->ORFs.clear();
            this->ORFs.push_back(ORF - 1);
        }

        this->m = m;
        this->n = n;
        this->k = k;
        this->S = k;
        this->S = S + ((S - 1) / this->m) * (this->n - this->m);
        this->hasher = new IntegerHasher(k);
        this->hash_mode = 1;
        this->canonical = true;
        this->slicing_mode = "skipmers";
    }

    Skipmers(const std::string &filename, unsigned int chunk_size, uint8_t m, uint8_t n, uint8_t k, int ORF = 0) {
        if (n < 1 or n < m || k < m || k % m != 0) {
            std::cerr << "Error: invalid skip-mer shape!"
                      << "Conditions: 0 < m <= n < k & k must be multiple of m" << std::endl;
            exit(1);
        }

        if (ORF) {
            this->ORFs.clear();
            this->ORFs.push_back(ORF - 1);
        }

        this->m = m;
        this->n = n;
        this->k = k;
        this->S = k;
        this->S = S + ((S - 1) / this->m) * (this->n - this->m);
        this->fileName = filename;
        this->chunk_size = chunk_size;
        this->initialize_kSeq();
        this->hasher = new IntegerHasher((int) k);
        this->hash_mode = 1;
        this->canonical = true;
        this->slicing_mode = "skipmers";
    }

    void setHashingMode(int hash_mode, bool canonical = true) {
        this->hash_mode = hash_mode;
        this->canonical = canonical;
        if (hash_mode == 0) hasher = (new MumurHasher(2038074761));
        else if (hash_mode == 1) {
            if (canonical) hasher = (new IntegerHasher(k));
            else hasher = (new noncanonical_IntegerHasher(k));
        } else if (hash_mode == 2) {
            if (canonical) {
                hasher = (new TwoBitsHasher(k));
            } else {
                hasher = (new noncanonical_TwoBitsHasher(k));
            }
        } else {
            hasher = (new IntegerHasher(k));
        }

    }

    void seq_to_kmers(std::string &seq, std::vector<kmer_row> &kmers);

    int get_kSize() {
        return this->k;
    }

    ~Skipmers(){}
};


/* 
--------------------------------------------------------
                        Minimizers
--------------------------------------------------------
*/

typedef struct mkmh_minimizer {
    uint64_t pos;
    uint32_t length;
    std::string seq;

    bool operator<(const mkmh_minimizer &rhs) const { return seq < rhs.seq; };
} mkmh_minimizer;


class Minimizers : public kmerDecoder {
private:
    int k, w;

    void extractKmers();

    struct mkmh_kmer_list_t {
        char **kmers;
        int length;
        int k;

        mkmh_kmer_list_t() {

        };

        mkmh_kmer_list_t(int length, int k) {
            this->length = length;
            this->k = k;
            this->kmers = new char *[length];
        };

        ~mkmh_kmer_list_t() {
            for (int i = 0; i < this->length; ++i) {
                delete[] this->kmers[i];
            }
            delete[] this->kmers;
        };
    };

protected:
    std::vector<mkmh_minimizer> kmer_tuples(std::string seq, int k);

    mkmh_kmer_list_t kmerize(char *seq, int seq_len, int k);

    std::vector<std::string> kmerize(std::string seq, int k);

    void kmerize(char *seq, const int &seq_len, const int &k, char **kmers, int &kmer_num);

    template<typename T>
    std::vector<T> v_set(std::vector<T> kmers);

public:
    Minimizers(const std::string &filename, unsigned int chunk_size, int k, int w) {
        this->k = k;
        this->w = w;
        this->fileName = filename;
        this->chunk_size = chunk_size;
        this->initialize_kSeq();
        this->hasher = new IntegerHasher(k);
        this->hash_mode = 1;
        this->canonical = true;
        this->slicing_mode = "minimizers";
    }

    Minimizers(int k, int w) {
        this->k = k;
        this->w = w;
        this->hasher = new IntegerHasher(k);
        this->hash_mode = 1;
        this->canonical = true;
        this->slicing_mode = "minimizers";
    }

    void setHashingMode(int hash_mode, bool canonical = true) {
        this->hash_mode = hash_mode;
        this->canonical = canonical;
        if (hash_mode == 0) hasher = (new MumurHasher(2038074761));
        else if (hash_mode == 1) {
            if (canonical) hasher = (new IntegerHasher(k));
            else hasher = (new noncanonical_IntegerHasher(k));
        } else if (hash_mode == 2) {
            if (canonical) {
                hasher = (new TwoBitsHasher(k));
            } else {
                hasher = (new noncanonical_TwoBitsHasher(k));
            }
        } else {
            hasher = (new IntegerHasher(k));
        }
    }

    std::vector<mkmh_minimizer> getMinimizers(std::string &seq);


    void seq_to_kmers(std::string &seq, std::vector<kmer_row> &kmers);

    int get_kSize() {
        return this->k;
    }

    static kmerDecoder *initialize_hasher(int kmer_size, int hash_mode = 1);

    ~Minimizers(){}

};


/* 
--------------------------------------------------------
                        AA Kmers (Protein Seqs)
--------------------------------------------------------
*/


class aaKmers : public kmerDecoder {

private:
    unsigned kSize{};

    void extractKmers() override;

public:

    explicit aaKmers(int k_size, int hash_mode = 1) : kSize(k_size) {

        if(kSize > 11){
            throw "can't use aaKmer > 11";
        }

        this->hasher = new aaHasher(kSize);
        this->slicing_mode = "kmers";
        this->hash_mode = 1;
        this->canonical = true;
    };

    aaKmers(const std::string &filename, unsigned int chunk_size, int kSize) {

        if(kSize > 11){
            throw "can't use aaKmer > 11";
        }

        this->kSize = kSize;
        this->fileName = filename;
        this->chunk_size = chunk_size;
        this->initialize_kSeq();
        this->hasher = new aaHasher(kSize);
        this->hash_mode = 1;
        this->canonical = true;
        this->slicing_mode = "kmers";
    }

    void seq_to_kmers(std::string &seq, std::vector<kmer_row> &kmers) override;


     void setHashingMode(int hash_mode, bool canonical = true) {
        
    }

    int get_kSize() {
        return this->kSize;
    }

    ~aaKmers() override{}

};
