#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <list>
#include <algorithm>
#include "sim.h"

using namespace std;

cache_params_t params;
int l2reads = 0, l2writes = 0;

void printbuffer(const Cache* cache) {
    if (!cache || params.PREF_N == 0 || params.PREF_M == 0) return;

    cout << "===== Stream Buffer(s) contents =====" << endl;

    for (const auto& prefetcher : cache->getBuffers()) {
        cout << " ";
        for (const auto& addr : prefetcher.buffer) {
            cout << hex << addr << " ";
        }
        cout << "\n";
    }

    cout << dec; 
}






CacheBlock::CacheBlock() : tag(0), dirtybit(false), valid(false) {}
CacheBlock::CacheBlock(uint32_t t, bool d, bool v) : tag(t), dirtybit(d), valid(v) {}

Prefetcher::Prefetcher(int m) : valid(false), pref_m(m) {
    buffer.resize(m);
}

Cache::Cache(int num_sets, int associativity, bool lastlevel)
    : num_sets(num_sets), associativity(associativity), writeBackCount(0),
      reads(0), readmiss(0), writes(0), writemiss(0),
      StreamBufferHits(0), Prefetches(0),
      lastlevel(lastlevel) {
    sets.resize(num_sets);
    if (lastlevel && params.PREF_N > 0 && params.PREF_M > 0) {
    buffers.resize(params.PREF_N, Prefetcher(params.PREF_M));
    lrubuffer.resize(params.PREF_N);
    for (size_t i = 0; i < params.PREF_N; ++i) {
        lrubuffer[i] = i;
    }
}

}

Cache::~Cache() {}

void Cache::updateLRU(std::list<CacheBlock>& set, std::list<CacheBlock>::iterator block) {
    set.splice(set.begin(), set, block);
}

void Cache::readandwrite(uint32_t addr, char rw, Cache* next_level) {
    int indexBits = log2(num_sets);
    int blockOffsetBits = log2(params.BLOCKSIZE);

    int index = (addr >> blockOffsetBits) & ((1 << indexBits) - 1);
    uint32_t tag = addr >> (blockOffsetBits + indexBits);

    auto& set = sets[index];

    bool cacheHit = false;
    list<CacheBlock>::iterator hitBlock = set.end();
   
    for (auto it = set.begin(); it != set.end(); ++it) {
        if (it->tag == tag && it->valid) {
            cacheHit = true;
            hitBlock = it;
            break;
        }
    }

    bool bufferHit = false;
    size_t bufferHitIndex = 0;
    uint32_t bufferaddr = addr >> blockOffsetBits;

    if (lastlevel && params.PREF_M > 0 && params.PREF_N > 0) {
        uint32_t blockAddr = addr >> blockOffsetBits;
        for (size_t i = 0; i < buffers.size(); ++i) {
            auto it = std::find(buffers[i].buffer.begin(), buffers[i].buffer.end(), blockAddr);
            if (it != buffers[i].buffer.end()) {
                bufferHit = true;
                bufferHitIndex = i;
                break;
            }
        }
    }

    if (cacheHit) { 
        if (bufferHit) { 
            updateBufferLRU(bufferHitIndex); 
            bufferprefetch(bufferHitIndex, addr); 
        }

        if (rw == 'w') hitBlock->dirtybit = true;

        updateLRU(set, hitBlock);

        if (rw == 'r') reads++; 
        else writes++; 
    } 
    else {
        if (bufferHit) { 
            updateBufferLRU(bufferHitIndex); 
            bufferprefetch(bufferHitIndex, addr);

            if (std::distance(set.begin(), set.end()) >= associativity)
                evictblock(set, next_level, index, blockOffsetBits, indexBits);

            bool d = rw == 'w';

            set.push_front(CacheBlock(tag, d, true));

            if (rw == 'r') reads++;
            else writes++;

            StreamBufferHits++;
            Prefetches++;
        } else { 
            if (rw == 'r') {
                reads++;
                readmiss++;
            } else {
                writes++;
                writemiss++;
            }
            
            if (std::distance(set.begin(), set.end()) >= associativity) {   
                evictblock(set, next_level, index, blockOffsetBits, indexBits);
            }
            
            if (next_level) { 
                next_level->readandwrite(addr, 'r', nullptr);
            } else main_mem++;

            set.push_front(CacheBlock(tag, rw == 'w', true));

            if (lastlevel)
                fillbuffer(addr);
        }
    }
}

void Cache::fillbuffer(uint32_t addr) {
    if (params.PREF_N == 0 || params.PREF_M == 0) return;
    size_t bufferIndex = getLeastRecentlyUsedStreamBuffer();
    auto& buffer1 = buffers[bufferIndex];
    
    buffer1.buffer.clear(); 

    int blockOffsetBits = log2(params.BLOCKSIZE);
    uint32_t startAddr = (addr >> blockOffsetBits) + 1;
    for (uint32_t i = 0; i < params.PREF_M; ++i) {  
        uint32_t prefetchAddr = startAddr + i;  
        buffer1.buffer.push_back(prefetchAddr);  
        ++Prefetches;  
    }
    
    updateBufferLRU(bufferIndex);  
}



void Cache::bufferprefetch(size_t bufferHitIndex, uint32_t addr) {
    auto& hit_buffer = buffers[bufferHitIndex].buffer;
    

    int blockOffsetBits = log2(params.BLOCKSIZE);
    uint32_t blockAddr = addr >> blockOffsetBits;

    auto it = std::find(hit_buffer.begin(), hit_buffer.end(), blockAddr);
    if (it != hit_buffer.end()) {
        hit_buffer.erase(hit_buffer.begin(), ++it);
        uint32_t prefetchAddr = blockAddr + params.PREF_M;
        hit_buffer.push_back(prefetchAddr);
    }

    updateBufferLRU(bufferHitIndex);  
}

void Cache::updateBufferLRU(size_t bufferIndex) {
    auto it = std::find(lrubuffer.begin(), lrubuffer.end(), bufferIndex);
    if (it != lrubuffer.end()) {
        lrubuffer.erase(it);
    }
    lrubuffer.insert(lrubuffer.begin(), bufferIndex);


}



size_t Cache::getMostRecentlyUsedStreamBuffer() const {
    return !lrubuffer.empty() ? lrubuffer.front() : 0;
}

size_t Cache::getLeastRecentlyUsedStreamBuffer() const {
    return !lrubuffer.empty() ? lrubuffer.back() : 0;
}

bool Cache::evictblock(list<CacheBlock>& set, Cache* next_level, int index, int blockOffsetBits, int indexBits) {
    auto lruIt = --set.end();
    bool d_bit = false;  

    if (lruIt->dirtybit) {
        uint32_t evictAddr = (lruIt->tag << (blockOffsetBits + indexBits)) | (index << blockOffsetBits);
        d_bit = true;
        
        if (next_level) {
            l2writes++;
            next_level->readandwrite(evictAddr, 'w', nullptr);
        } else {
            main_mem++;
        }
        
        writeBackCount++;
    }
    
    set.pop_back();
    
    return d_bit; 
}

void Cache::printcache(const char* cache_name) const {
    cout << endl << "===== " << cache_name << " contents =====" << endl;
    for(size_t i = 0; i < num_sets; ++i) {
        if(sets[i].empty()) continue;

        cout << "set" << setw(7) << dec << i << ": ";
        for(const auto& block : sets[i]) {
            if(block.valid) {
                cout << hex << block.tag;
                if(block.dirtybit) {
                    cout << " D   ";
                } else {
                    cout << "     ";
                }
            }
        }
        cout << endl;
    }
}

int Cache::getWriteBackCount() const {
    return writeBackCount;
}



int main(int argc, char* argv[]) {
    FILE* fp;
    char* trace_file;
    char rw;
    uint32_t addr;

    if (argc != 9) {
        printf("Error: Expected 8 command-line arguments but was provided %d.\n", argc - 1);
        exit(EXIT_FAILURE);
    }

    params.BLOCKSIZE = atoi(argv[1]);
    params.L1_SIZE = atoi(argv[2]);
    params.L1_ASSOC = atoi(argv[3]);
    params.L2_SIZE = atoi(argv[4]);
    params.L2_ASSOC = atoi(argv[5]);
    params.PREF_N = atoi(argv[6]);
    params.PREF_M = atoi(argv[7]);
    trace_file = argv[8];

    if (params.L1_SIZE == 0 || params.L1_ASSOC == 0) {
        printf("Invalid cache input for L1\n");
        return 0;
    }

    int l1_sets = params.L1_SIZE / params.BLOCKSIZE / params.L1_ASSOC;
    int l2_sets = params.L2_SIZE != 0 && params.L2_ASSOC != 0
                  ? params.L2_SIZE / params.BLOCKSIZE / params.L2_ASSOC
                  : 0;
    bool use_l2 = params.L2_SIZE != 0 && params.L2_ASSOC != 0;

    Cache l1_cache(l1_sets, params.L1_ASSOC, !use_l2);
    Cache* l2_cache = use_l2 ? new Cache(l2_sets, params.L2_ASSOC, true) : nullptr;

    fp = fopen(trace_file, "r");

    if (!fp) {
        printf("Error: Unable to open file %s\n", trace_file);
        exit(EXIT_FAILURE);
    }

   printf("===== Simulator configuration =====\n");
   printf("BLOCKSIZE:  %u\n", params.BLOCKSIZE);
   printf("L1_SIZE:    %u\n", params.L1_SIZE);
   printf("L1_ASSOC:   %u\n", params.L1_ASSOC);
   printf("L2_SIZE:    %u\n", params.L2_SIZE);
   printf("L2_ASSOC:   %u\n", params.L2_ASSOC);
   printf("PREF_N:     %u\n", params.PREF_N);
   printf("PREF_M:     %u\n", params.PREF_M);
   printf("trace_file: %s\n", trace_file);


	while (fscanf(fp," %c %x",&rw,&addr)==2) {

	    l1_cache.readandwrite(addr,rw,l2_cache);


	}

	fclose(fp);

	l1_cache.printcache("L1");


	if (use_l2 && l2_cache) {
    l2_cache->printcache("L2");
    printbuffer(l2_cache); 

} else {
    printbuffer(&l1_cache);  
}

cout << endl << "===== Measurements =====" << endl;
cout << setprecision(4) << fixed;

// L1 Cache Measurements
cout << "a. L1 reads:                   " << dec << l1_cache.getReads() << endl;
cout << "b. L1 read misses:             " << dec << l1_cache.getReadMisses() << endl;
cout << "c. L1 writes:                  " << dec << l1_cache.getWrites() << endl;
cout << "d. L1 write misses:            " << dec << l1_cache.getWriteMisses() << endl;
cout << "e. L1 miss rate:               " << static_cast<double>(l1_cache.getReadMisses() + l1_cache.getWriteMisses()) / (l1_cache.getReads() + l1_cache.getWrites()) << endl;
cout << "f. L1 writebacks:              " << dec << l1_cache.getWriteBackCount() << endl;
cout << "g. L1 prefetches:              " << dec << l1_cache.getPrefetches() << endl;

// L2 Cache Measurements 
if (use_l2 && l2_cache) {
cout << "h. L2 reads (demand):          " << dec << l2_cache->getReads()<< endl;
cout << "i. L2 read misses (demand):    " << dec << l2_cache->getReadMisses() << endl;
cout << "j. L2 reads (prefetch):        " << dec << l2_cache->getPrefetchReads() << endl;
cout << "k. L2 read misses (prefetch):  " << dec << l2_cache->getPrefetchReadMisses() << endl;
cout << "l. L2 writes:                  " << dec << l2_cache->getWrites() << endl;
cout << "m. L2 write misses:            " << dec << l2_cache->getWriteMisses() << endl;
cout << "n. L2 miss rate:               " << dec << static_cast<double>(l2_cache->getReadMisses()) / (l2_cache->getReads())<< endl;
cout << "o. L2 writebacks:              " << dec << l2_cache->getWriteBackCount() << endl;
cout << "p. L2 prefetches:              " << dec << l2_cache->getPrefetches() << endl;
} else {
cout << "h. L2 reads (demand):          0" << endl;
cout << "i. L2 read misses (demand):    0" << endl;
cout << "j. L2 reads (prefetch):        0" << endl;
cout << "k. L2 read misses (prefetch):  0" << endl;
cout << "l. L2 writes:                  0" << endl;
cout << "m. L2 write misses:            0" << endl;
cout << "n. L2 miss rate:               0.0000" << endl;
cout << "o. L2 writebacks:              0" << endl;
cout << "p. L2 prefetches:              0" << endl;
}


uint32_t memoryTraffic = l1_cache.getReadMisses() + l1_cache.getWriteMisses() + l1_cache.getWriteBackCount();

if(!use_l2)

{

cout << "q. memory traffic:             " << dec << memoryTraffic << endl;

}

if (use_l2 && l2_cache) {

    memoryTraffic = l2_cache->getReadMisses() + l2_cache->getWriteMisses() + l2_cache->getWriteBackCount();

cout << "q. memory traffic:             " << dec << memoryTraffic << endl;



}

	delete l2_cache;
    
    return 0;
}
