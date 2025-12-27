#ifndef SIM_H
#define SIM_H

#include <cstdint>
#include <list>
#include <vector>
#include <iostream>

struct cache_params_t {
    uint32_t BLOCKSIZE;
    uint32_t L1_SIZE;
    uint32_t L1_ASSOC;
    uint32_t L2_SIZE;
    uint32_t L2_ASSOC;
    uint32_t PREF_N;
    uint32_t PREF_M;
};

class CacheBlock {
public:
    uint32_t tag;
    bool dirtybit;
    bool valid;

    CacheBlock();
    CacheBlock(uint32_t t, bool d, bool v);
};

class Prefetcher {
public:
    bool valid;
    int pref_m;

    std::list<uint32_t> buffer;
    Prefetcher(int m);
};

class Cache {
private:
    int num_sets;
    int associativity;
    
    std::vector<std::list<CacheBlock>> sets;
    int writeBackCount;
    int reads, readmiss, writes, writemiss, readsl2;
    int StreamBufferHits;
    int Prefetches;
    bool lastlevel;

    std::vector<Prefetcher> buffers;
    std::list<size_t> buffer;

    void updateLRU(std::list<CacheBlock>& set, std::list<CacheBlock>::iterator block);
    
    void fillbuffer(uint32_t addr);
    void updateBufferLRU(size_t bufferIndex);
    size_t getMostRecentlyUsedStreamBuffer() const;
    size_t getLeastRecentlyUsedStreamBuffer() const;
    void bufferprefetch(size_t bufferHitIndex, uint32_t addr);
    

public:
    int main_mem;
    Cache(int num_sets, int associativity, bool lastlevel);
    ~Cache();

    void readandwrite(uint32_t addr, char rw, Cache* next_level);
    void printcache(const char* cache_name) const;
    void printmeasurements(const char* cache_name) const;
    bool evictblock(std::list<CacheBlock>& set, Cache* next_level, int index, int blockOffsetBits, int indexBits);
    int getWriteBackCount() const;
    std::vector<size_t> lrubuffer;

    const std::vector<Prefetcher>& getBuffers() const { return buffers; }
    int getReads() const { return reads; }
    int getReadMisses() const { return readmiss; }
    int getWrites() const { return writes; }
    int getWriteMisses() const { return writemiss; }

    int getPrefetches() const { return Prefetches; }
    int getPrefetchReads() const { return 0; } 
    int getPrefetchReadMisses() const { return 0; }

};

extern cache_params_t params;

void printbuffer(const Cache* cache);

#endif // SIM_H
