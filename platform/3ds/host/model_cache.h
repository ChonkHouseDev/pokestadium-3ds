// Cache de modelos horneados (F1 del renderizador nativo).
//
// Un "lote" es lo que gfx_convert junta entre dos cambios de estado de un modelo con huesos: una secuencia de
// mallas [hueso, malla] cuyo resultado (vertices nativos con su hueso, indices, modos de geometria) solo depende
// de la memoria de esas mallas y sus vertices. Aqui se guarda ese resultado ya serializado para no rehacerlo
// cada cuadro.
//
// Identidad: la clave son las direcciones (no el contenido): mallas en orden con su hueso, huesos al empezar y
// al terminar, y los segmentos que usaron sus G_VTX. Nunca se recalcula un hash del contenido.
// Vida util: el juego avisa (ps_mem_event) cada vez que un rango de su RAM cambia de dueno o de contenido; un
// aviso que toca una malla o sus vertices invalida el lote. La generacion de un lote es su id: un id nunca se
// reusa, asi que un id guardado deja de encontrarse en cuanto el lote se invalida.
#pragma once
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mc {

struct Token { int32_t bone; const uint8_t* mesh; };
// Las 64 ranuras de vertices siguen vivas entre lotes del mismo modelo: un lote puede usar vertices que cargo el
// anterior (las uniones entre huesos). SlotOut es lo que el lote deja en una ranura; SlotIn, una ranura que el
// lote lee sin haberla cargado: el acierto exige que la ranura tenga lo mismo (mismo vertice de origen y hueso).
struct SlotOut { const uint8_t* src; uint8_t slot; uint8_t bone; uint8_t v[16]; uint8_t pad[2]; };
struct SlotIn { const uint8_t* src; uint8_t slot; uint8_t bone; uint8_t valid; uint8_t pad; };
struct Range { uintptr_t lo; uint32_t len; };

struct Key {
    const Token* tokens;
    uint32_t nTokens;
    uint32_t startBones, endBones;
    uint32_t hash;
};

struct Hit {
    uint32_t id;
    const uint8_t* body;  // lo que skinFlush escribe despues de los apuntadores de huesos
    uint32_t bodyBytes;
    uint32_t nSegs;
    const SlotOut* slots;
    uint32_t nSlots;
    const SlotIn* inputs;
    uint32_t nInputs;
    uint32_t tris;
    uint32_t segMask;     // segmentos que resolvieron sus G_VTX (para usedSegs del convertidor)
};

enum Uncacheable { UC_TOO_BIG, UC_EMPTY, UC_COUNT };

struct Stats {
    uint32_t hits, misses, inputMisses, baked, invalidated, evicted, idleDropped, allocFails, budgetFails, replaced;
    uint32_t gpuBuilt, gpuRebuilt, gpuAllocFails, gpuBytes;
    uint32_t uncacheable[UC_COUNT];
    uint32_t events, eventHits;
    uint32_t verifyOk, verifyBad;
    uint32_t entries, bytes, peakBytes;
};

class ModelCache {
public:
    void configure(uint32_t budgetBytes, bool verify) { budget_ = budgetBytes; verify_ = verify; }
    bool enabled() const { return budget_ != 0; }
    bool verify() const { return verify_; }

    static uint32_t hashKey(const Token* t, uint32_t n, uint32_t startBones, uint32_t endBones);
    // acierto: misma secuencia exacta y mismos valores en los segmentos que uso
    bool lookup(const Key& k, const uint32_t* segments, Hit* out);
    // regresa el id del lote, 0 si no se guardo (presupuesto, memoria)
    uint32_t store(const Key& k, uint32_t segMask, const uint32_t* segments, const uint8_t* body, uint32_t bodyBytes,
                   uint32_t nSegs, const SlotOut* slots, uint32_t nSlots, const SlotIn* inputs, uint32_t nInputs, uint32_t tris,
                   const Range* ranges, uint32_t nRanges);
    // el acierto no sirvio: las ranuras de entrada no coinciden (se cuenta como fallo)
    void noteInputMismatch() { st_.hits--; st_.misses++; st_.inputMisses++; }
    void noteUncacheable(Uncacheable why) { st_.uncacheable[why]++; }
    void noteVerify(bool ok) { (ok ? st_.verifyOk : st_.verifyBad)++; }
    void onMemEvent(uint32_t kind, uintptr_t addr, uint32_t size);
    // F2: copia para el GPU de un lote (vertices e indices en memoria lineal), del mismo dueno que el lote: se
    // libera con el (unos cuadros despues: el GPU puede estar leyendo el cuadro anterior). key identifica con que
    // estado se armo (escala de texturas); otra key = rearmar.
    void* gpuGet(uint32_t id, uint32_t key);
    void* gpuAttach(uint32_t id, uint32_t key, uint32_t bytes); // nullptr: sin memoria o sin presupuesto
    bool alive(uint32_t id) const { return byId_.count(id) != 0; }
    const uint8_t* bodyOf(uint32_t id) const { auto it = byId_.find(id); return it == byId_.end() ? nullptr : it->second.mem; }
    void beginFrame();
    // contadores desde la ultima llamada (entries/bytes/peakBytes son el estado actual)
    Stats takeStats();
    void clear();

private:
    struct Entry {
        uint32_t id, hash, startBones, endBones, segMask, segVals[16];
        std::vector<Token> tokens;
        std::vector<Range> ranges;
        uint8_t* mem;  // cuerpo y luego las ranuras
        uint32_t bodyBytes, nSegs, nSlots, nInputs, tris, lastUse, bytes;
        uint8_t* gpu = nullptr;
        uint32_t gpuKey = 0, gpuBytes = 0;
    };
    struct Deferred { uint8_t* p; uint32_t frame; };
    std::vector<Deferred> deferred_;
    void deferFree(uint8_t* p) { if (p) deferred_.push_back({ p, frame_ }); }
    static constexpr uint32_t kPageShift = 14; // 16 KB por pagina del indice de rangos
    static constexpr uint32_t kIdleFrames = 1800;
    void drop(uint32_t id);
    void evictFor(uint32_t bytes);
    static uint8_t* allocMem(uint32_t bytes);
    static void freeMem(uint8_t* p);

    uint32_t budget_ = 0, used_ = 0, frame_ = 0, nextId_ = 1;
    bool verify_ = false;
    std::unordered_map<uint32_t, Entry> byId_;
    std::unordered_map<uint32_t, uint32_t> byHash_;              // hash de la clave -> id
    std::unordered_map<uint32_t, std::vector<uint32_t>> pages_;  // pagina de RAM -> ids que dependen de ella
    Stats st_{};
};

} // namespace mc
