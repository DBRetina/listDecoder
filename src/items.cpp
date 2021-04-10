#include "kmerDecoder.hpp"
#include "gzstream.h"
#include <sstream>


std::istream& operator>>(std::istream& str, CSVRow& data)
{
    data.readNextRow(str);
    return str;
}

Items::Items(const std::string &filename) {
        this->chunk_size = 1; // To allow duplicate parents
        this->filename = filename;
        file = ifstream(this->filename);
        // Skip header
        file >> row;
}

void Items::extractKmers(){
    this->next_chunk();
}

void Items::next_chunk(){
    
    std::string parent, child, metadata;

    while(this->file.peek() != EOF)
    {
        this->file >> row;
        parent = row[0];
        child = row[1];

        uint64_t childHash = this->child_hasher(child);
        kmer_row itemRow;
        itemRow.str = child;
        itemRow.hash = childHash;
        this->kmers[parent].emplace_back(itemRow);
        this->hash_to_str[childHash] = child;

    }

    if(this->kmers.size() < this->chunk_size){
        this->END = true;
        this->FILE_END = true;
        this->file.close();
    }else{
        this->END = false;
        this->FILE_END = false;
    }

}

bool Items::end(){
    return this->FILE_END;
}

void Items::setHashingMode(int hash_mode, bool canonical){

}

void Items::seq_to_kmers(std::string &seq, std::vector<kmer_row> &kmers){

}