#include "model_cache.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#ifdef __3DS__
#include <3ds.h>
#endif

namespace mc {

// Cuerpos en memoria lineal: el PICA200 la puede leer directo (lo usara F2) y el heap normal es el que menos
// sobra en el Old 3DS. Se deja siempre un colchon para texturas y buffers del GPU.
uint8_t* ModelCache::allocMem(uint32_t bytes) {
#ifdef __3DS__
    if (linearSpaceFree() < bytes + 4u * 1024u * 1024u) return nullptr;
    return static_cast<uint8_t*>(linearAlloc(bytes));
#else
    return static_cast<uint8_t*>(std::malloc(bytes));
#endif
}
void ModelCache::freeMem(uint8_t* p) {
#ifdef __3DS__
    linearFree(p);
#else
    std::free(p);
#endif
}

uint32_t ModelCache::hashKey(const Token* t, uint32_t n, uint32_t startBones, uint32_t endBones) {
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix(n); mix(startBones); mix(endBones);
    for (uint32_t i = 0; i < n; i++) { mix((uint32_t)t[i].bone); mix((uint32_t)reinterpret_cast<uintptr_t>(t[i].mesh)); }
    return h ? h : 1;
}

bool ModelCache::lookup(const Key& k, const uint32_t* segments, Hit* out) {
    auto h = byHash_.find(k.hash);
    if (h == byHash_.end()) { st_.misses++; return false; }
    Entry& e = byId_.at(h->second);
    bool same = e.startBones == k.startBones && e.endBones == k.endBones && e.tokens.size() == k.nTokens;
    for (uint32_t i = 0; same && i < k.nTokens; i++) same = e.tokens[i].bone == k.tokens[i].bone && e.tokens[i].mesh == k.tokens[i].mesh;
    for (uint32_t s = 0; same && s < 16; s++)
        if (e.segMask & (1u << s)) same = segments[s] == e.segVals[s];
    if (!same) { st_.misses++; return false; }
    e.lastUse = frame_;
    out->id = e.id;
    out->body = e.mem;
    out->bodyBytes = e.bodyBytes;
    out->nSegs = e.nSegs;
    out->slots = reinterpret_cast<const SlotOut*>(e.mem + e.bodyBytes);
    out->nSlots = e.nSlots;
    out->inputs = reinterpret_cast<const SlotIn*>(e.mem + e.bodyBytes + e.nSlots * sizeof(SlotOut));
    out->nInputs = e.nInputs;
    out->tris = e.tris;
    out->segMask = e.segMask;
    st_.hits++;
    return true;
}

void ModelCache::evictFor(uint32_t bytes) {
    while (used_ + bytes > budget_ && !byId_.empty()) {
        uint32_t victim = 0, oldest = 0xFFFFFFFFu;
        for (auto& kv : byId_)
            if (kv.second.lastUse < oldest) { oldest = kv.second.lastUse; victim = kv.first; }
        // lo que se uso en este mismo cuadro no se saca: si no cabe, no se guarda el nuevo
        if (oldest == frame_) return;
        drop(victim);
        st_.evicted++;
    }
}

uint32_t ModelCache::store(const Key& k, uint32_t segMask, const uint32_t* segments, const uint8_t* body, uint32_t bodyBytes,
                           uint32_t nSegs, const SlotOut* slots, uint32_t nSlots, const SlotIn* inputs, uint32_t nInputs,
                           uint32_t tris, const Range* ranges, uint32_t nRanges) {
    if (!budget_) return 0;
    auto old = byHash_.find(k.hash);
    if (old != byHash_.end()) { drop(old->second); st_.replaced++; } // misma clave, otra secuencia o segmentos
    // cuerpo, ranuras que deja y ranuras que lee (en ese orden; el cuerpo es multiplo de 4 y las demas de 4/8)
    const uint32_t memBytes = bodyBytes + nSlots * (uint32_t)sizeof(SlotOut) + nInputs * (uint32_t)sizeof(SlotIn);
    const uint32_t bytes = memBytes + (uint32_t)sizeof(Entry) + k.nTokens * (uint32_t)sizeof(Token) + nRanges * (uint32_t)sizeof(Range);
    if (bytes > budget_ / 4) { st_.uncacheable[UC_TOO_BIG]++; return 0; }
    evictFor(bytes);
    if (used_ + bytes > budget_) { st_.budgetFails++; return 0; }
    uint8_t* mem = allocMem(memBytes ? memBytes : 4);
    if (!mem) { st_.allocFails++; return 0; }
    std::memcpy(mem, body, bodyBytes);
    if (nSlots) std::memcpy(mem + bodyBytes, slots, nSlots * sizeof(SlotOut));
    if (nInputs) std::memcpy(mem + bodyBytes + nSlots * sizeof(SlotOut), inputs, nInputs * sizeof(SlotIn));

    const uint32_t id = nextId_++;
    if (!nextId_) nextId_ = 1;
    Entry& e = byId_[id];
    e.id = id;
    e.hash = k.hash;
    e.startBones = k.startBones;
    e.endBones = k.endBones;
    e.segMask = segMask;
    for (uint32_t s = 0; s < 16; s++) e.segVals[s] = (segMask & (1u << s)) ? segments[s] : 0;
    e.tokens.assign(k.tokens, k.tokens + k.nTokens);
    e.ranges.assign(ranges, ranges + nRanges);
    e.mem = mem;
    e.bodyBytes = bodyBytes;
    e.nSegs = nSegs;
    e.nSlots = nSlots;
    e.nInputs = nInputs;
    e.tris = tris;
    e.lastUse = frame_;
    e.bytes = bytes;
    byHash_[k.hash] = id;
    for (uint32_t i = 0; i < nRanges; i++) {
        const uint32_t p0 = (uint32_t)(ranges[i].lo >> kPageShift), p1 = (uint32_t)((ranges[i].lo + ranges[i].len - 1) >> kPageShift);
        for (uint32_t p = p0; p <= p1; p++) {
            auto& v = pages_[p];
            if (v.empty() || v.back() != id) v.push_back(id);
        }
    }
    used_ += bytes;
    if (used_ > st_.peakBytes) st_.peakBytes = used_;
    st_.baked++;
    return id;
}

void ModelCache::drop(uint32_t id) {
    auto it = byId_.find(id);
    if (it == byId_.end()) return;
    Entry& e = it->second;
    for (auto& r : e.ranges) {
        const uint32_t p0 = (uint32_t)(r.lo >> kPageShift), p1 = (uint32_t)((r.lo + r.len - 1) >> kPageShift);
        for (uint32_t p = p0; p <= p1; p++) {
            auto pg = pages_.find(p);
            if (pg == pages_.end()) continue;
            auto& v = pg->second;
            v.erase(std::remove(v.begin(), v.end(), id), v.end());
            if (v.empty()) pages_.erase(pg);
        }
    }
    auto h = byHash_.find(e.hash);
    if (h != byHash_.end() && h->second == id) byHash_.erase(h);
    freeMem(e.mem);
    deferFree(e.gpu);
    used_ -= e.bytes + e.gpuBytes;
    byId_.erase(it);
}

void ModelCache::onMemEvent(uint32_t kind, uintptr_t addr, uint32_t size) {
    (void)kind;
    st_.events++;
    if (!size || byId_.empty()) return;
    const uint32_t p0 = (uint32_t)(addr >> kPageShift), p1 = (uint32_t)((addr + size - 1) >> kPageShift);
    std::vector<uint32_t> hit;
    for (uint32_t p = p0; p <= p1; p++) {
        auto pg = pages_.find(p);
        if (pg == pages_.end()) continue;
        for (uint32_t id : pg->second) {
            const Entry& e = byId_.at(id);
            for (auto& r : e.ranges)
                if (addr < r.lo + r.len && r.lo < addr + size) { hit.push_back(id); break; }
        }
    }
    if (hit.empty()) return;
    st_.eventHits++;
    std::sort(hit.begin(), hit.end());
    hit.erase(std::unique(hit.begin(), hit.end()), hit.end());
    for (uint32_t id : hit) { drop(id); st_.invalidated++; }
}

void* ModelCache::gpuGet(uint32_t id, uint32_t key) {
    auto it = byId_.find(id);
    if (it == byId_.end() || !it->second.gpu || it->second.gpuKey != key) return nullptr;
    return it->second.gpu;
}

void* ModelCache::gpuAttach(uint32_t id, uint32_t key, uint32_t bytes) {
    auto it = byId_.find(id);
    if (it == byId_.end()) return nullptr;
    {
        Entry& e = it->second;
        if (e.gpu) { deferFree(e.gpu); used_ -= e.gpuBytes; e.gpu = nullptr; e.gpuBytes = 0; st_.gpuRebuilt++; }
    }
    evictFor(bytes);
    it = byId_.find(id); // evictFor no saca lo usado en este cuadro, pero por si acaso
    if (it == byId_.end()) return nullptr;
    if (used_ + bytes > budget_) { st_.budgetFails++; return nullptr; }
    uint8_t* p = allocMem(bytes);
    if (!p) { st_.gpuAllocFails++; return nullptr; }
    Entry& e = it->second;
    e.gpu = p;
    e.gpuKey = key;
    e.gpuBytes = bytes;
    used_ += bytes;
    if (used_ > st_.peakBytes) st_.peakBytes = used_;
    st_.gpuBuilt++;
    return p;
}

void ModelCache::beginFrame() {
    frame_++;
    // el GPU ya termino los cuadros de hace 3 o mas (cada cuadro espera al anterior al empezar)
    size_t keep = 0;
    for (auto& d : deferred_) {
        if (frame_ - d.frame >= 3) freeMem(d.p);
        else deferred_[keep++] = d;
    }
    deferred_.resize(keep);
    if ((frame_ & 255) != 0 || byId_.empty()) return;
    std::vector<uint32_t> idle;
    for (auto& kv : byId_)
        if (frame_ - kv.second.lastUse > kIdleFrames) idle.push_back(kv.first);
    for (uint32_t id : idle) { drop(id); st_.idleDropped++; }
}

Stats ModelCache::peekStats() const {
    Stats s = st_;
    s.entries = (uint32_t)byId_.size();
    s.bytes = used_;
    s.gpuBytes = 0;
    for (auto& kv : byId_) s.gpuBytes += kv.second.gpuBytes;
    return s;
}
Stats ModelCache::takeStats() {
    Stats s = st_;
    s.entries = (uint32_t)byId_.size();
    s.bytes = used_;
    s.gpuBytes = 0;
    for (auto& kv : byId_) s.gpuBytes += kv.second.gpuBytes;
    st_ = Stats{};
    st_.peakBytes = used_;
    return s;
}

void ModelCache::clear() {
    std::vector<uint32_t> ids;
    for (auto& kv : byId_) ids.push_back(kv.first);
    for (uint32_t id : ids) drop(id);
}

} // namespace mc
