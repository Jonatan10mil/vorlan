#include "QrCode.h"
#include <array>
#include <algorithm>

// ============================================================================
//  Generador de QR (modo byte, ECC nivel M, versiones 1..10).
//  Basado en el algoritmo estándar ISO/IEC 18004. Escrito de forma compacta.
// ============================================================================

namespace qr {
namespace {

// --- Tabla por versión (ECC nivel M): {eccPorBloque, nBloquesG1, datosG1, nBloquesG2, datosG2} ---
struct VerInfo { int ecc, g1n, g1d, g2n, g2d; };
constexpr VerInfo kVer[] = {
    { 0,0,0,0,0},                 // idx 0 sin uso
    {10, 1,16, 0, 0},             // v1
    {16, 1,28, 0, 0},             // v2
    {26, 1,44, 0, 0},             // v3
    {18, 2,32, 0, 0},             // v4
    {24, 2,43, 0, 0},             // v5
    {16, 4,27, 0, 0},             // v6
    {18, 4,31, 0, 0},             // v7
    {22, 2,38, 2,39},             // v8
    {22, 3,36, 2,37},             // v9
    {26, 4,43, 1,44},             // v10
};
constexpr int kMaxVer = 10;

int totalDataCodewords(int v) {
    const VerInfo &vi = kVer[v];
    return vi.g1n * vi.g1d + vi.g2n * vi.g2d;
}

// Posiciones de los patrones de alineación por versión.
const std::vector<int> &alignPositions(int v) {
    static const std::vector<int> tbl[] = {
        {},                 // 0
        {},                 // 1
        {6,18}, {6,22}, {6,26}, {6,30}, {6,34},
        {6,22,38}, {6,24,42}, {6,26,46}, {6,28,50},
    };
    return tbl[v];
}

// ---------------- GF(256) ----------------
struct GF {
    uint8_t exp[512], log[256];
    GF() {
        int x = 1;
        for (int i = 0; i < 255; ++i) {
            exp[i] = uint8_t(x);
            log[x] = uint8_t(i);
            x <<= 1;
            if (x & 0x100) x ^= 0x11D;
        }
        for (int i = 255; i < 512; ++i) exp[i] = exp[i - 255];
    }
    uint8_t mul(uint8_t a, uint8_t b) const {
        if (a == 0 || b == 0) return 0;
        return exp[log[a] + log[b]];
    }
};
const GF gf;

// Polinomio generador de grado `deg`, coeficientes con el término LÍDER primero
// ([1, g1, …, g_deg]) para que rsEcc pueda usar gen[j+1].
std::vector<uint8_t> genPoly(int deg) {
    std::vector<uint8_t> p{1};
    for (int i = 0; i < deg; ++i) {
        std::vector<uint8_t> np(p.size() + 1, 0);
        for (size_t j = 0; j < p.size(); ++j) {
            np[j] ^= gf.mul(p[j], gf.exp[i]);
            np[j + 1] ^= p[j];
        }
        p = np;
    }
    std::reverse(p.begin(), p.end());   // término independiente-primero → líder-primero
    return p;
}

// ECC Reed-Solomon de un bloque de datos.
std::vector<uint8_t> rsEcc(const std::vector<uint8_t> &data, int eccLen) {
    std::vector<uint8_t> gen = genPoly(eccLen);
    std::vector<uint8_t> res(eccLen, 0);
    for (uint8_t d : data) {
        uint8_t factor = d ^ res[0];
        res.erase(res.begin());
        res.push_back(0);
        for (int j = 0; j < eccLen; ++j)
            res[j] ^= gf.mul(gen[j + 1], factor);
    }
    return res;
}

// ---------------- Matriz ----------------
struct Matrix {
    int size;
    std::vector<std::vector<bool>> mod;   // negro/blanco
    std::vector<std::vector<bool>> fn;    // ¿módulo funcional?
    Matrix(int s) : size(s), mod(s, std::vector<bool>(s, false)),
                    fn(s, std::vector<bool>(s, false)) {}
    void set(int x, int y, bool v, bool func) { mod[y][x] = v; fn[y][x] = func; }
};

// Patrón buscador 7x7 + separador (1 módulo blanco). (ox,oy) = esquina superior
// izquierda del bloque 7x7.
void drawFinder(Matrix &m, int ox, int oy) {
    for (int dy = -1; dy <= 7; ++dy)
        for (int dx = -1; dx <= 7; ++dx) {
            int x = ox + dx, y = oy + dy;
            if (x < 0 || y < 0 || x >= m.size || y >= m.size) continue;
            bool black = false;
            if (dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6) {
                int d = std::max(std::abs(dx - 3), std::abs(dy - 3));
                black = (d != 2);   // anillo exterior + centro 3x3
            }
            m.set(x, y, black, true);
        }
}

void drawAlign(Matrix &m, int cx, int cy) {
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            int md = std::max(std::abs(dx), std::abs(dy));
            m.set(cx + dx, cy + dy, (md != 1), true);
        }
}

void drawFunction(Matrix &m, int version) {
    // Timing
    for (int i = 0; i < m.size; ++i) {
        if (!m.fn[6][i]) m.set(i, 6, i % 2 == 0, true);
        if (!m.fn[i][6]) m.set(6, i, i % 2 == 0, true);
    }
    // Finders + separadores (esquina superior izquierda de cada bloque 7x7)
    drawFinder(m, 0, 0);
    drawFinder(m, m.size - 7, 0);
    drawFinder(m, 0, m.size - 7);
    // Alineación
    const std::vector<int> &pos = alignPositions(version);
    for (int a : pos)
        for (int b : pos) {
            bool corner = (a == 6 && b == 6) ||
                          (a == 6 && b == pos.back()) ||
                          (a == pos.back() && b == 6);
            if (!corner) drawAlign(m, a, b);
        }
    // Módulo oscuro fijo
    m.set(8, m.size - 8, true, true);
    // Reservar zonas de formato (se rellenan luego)
    for (int i = 0; i < 9; ++i) {
        if (!m.fn[i][8]) m.set(8, i, false, true);
        if (!m.fn[8][i]) m.set(i, 8, false, true);
    }
    for (int i = 0; i < 8; ++i) {
        m.set(m.size - 1 - i, 8, false, true);
        m.set(8, m.size - 1 - i, false, true);
    }
    // Reservar zonas de versión (v>=7)
    if (version >= 7) {
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 3; ++j) {
                m.set(m.size - 11 + j, i, false, true);
                m.set(i, m.size - 11 + j, false, true);
            }
    }
}

void placeData(Matrix &m, const std::vector<uint8_t> &bits) {

    int bitIdx = 0;
    int total = int(bits.size());
    for (int right = m.size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;   // saltar columna de timing
        for (int vert = 0; vert < m.size; ++vert) {
            for (int j = 0; j < 2; ++j) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? m.size - 1 - vert : vert;
                if (m.fn[y][x]) continue;
                bool v = (bitIdx < total) ? bits[bitIdx] : false;
                m.mod[y][x] = v;
                ++bitIdx;
            }
        }
    }
}

bool maskBit(int mask, int x, int y) {
    switch (mask) {
        case 0: return (x + y) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (x + y) % 3 == 0;
        case 4: return (y / 2 + x / 3) % 2 == 0;
        case 5: return (x * y) % 2 + (x * y) % 3 == 0;
        case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
        case 7: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
    }
    return false;
}

void applyMask(Matrix &m, int mask) {
    for (int y = 0; y < m.size; ++y)
        for (int x = 0; x < m.size; ++x)
            if (!m.fn[y][x] && maskBit(mask, x, y))
                m.mod[y][x] = !m.mod[y][x];
}

// BCH de 15 bits para la información de formato (ECC M => bits "00").
int formatBits(int mask) {
    int data = (0b00 << 3) | mask;   // 00 = nivel M
    int rem = data;
    for (int i = 0; i < 10; ++i)
        rem = (rem << 1) ^ (((rem >> 9) & 1) * 0x537);
    int bits = ((data << 10) | rem) ^ 0x5412;
    return bits;
}

void drawFormat(Matrix &m, int mask) {
    int bits = formatBits(mask);
    for (int i = 0; i <= 5; ++i) m.mod[i][8] = (bits >> i) & 1;
    m.mod[7][8] = (bits >> 6) & 1;
    m.mod[8][8] = (bits >> 7) & 1;
    m.mod[8][7] = (bits >> 8) & 1;
    for (int i = 9; i < 15; ++i) m.mod[8][14 - i] = (bits >> i) & 1;
    for (int i = 0; i < 8; ++i) m.mod[8][m.size - 1 - i] = (bits >> i) & 1;
    for (int i = 8; i < 15; ++i) m.mod[m.size - 15 + i][8] = (bits >> i) & 1;
    m.mod[m.size - 8][8] = true;   // módulo oscuro
}

void drawVersion(Matrix &m, int version) {
    if (version < 7) return;
    // BCH(18,6) de la versión.
    int rem = version;
    for (int i = 0; i < 12; ++i)
        rem = (rem << 1) ^ (((rem >> 11) & 1) * 0x1F25);
    int bits = (version << 12) | rem;
    for (int i = 0; i < 18; ++i) {
        bool b = (bits >> i) & 1;
        int a = i / 3, c = i % 3;
        m.mod[a][m.size - 11 + c] = b;
        m.mod[m.size - 11 + c][a] = b;
    }
}

int penalty(const Matrix &m) {
    int score = 0, n = m.size;
    // Regla 1: rachas de 5+ en filas y columnas.
    for (int y = 0; y < n; ++y)
        for (int dir = 0; dir < 2; ++dir) {
            int run = 1; bool prev = dir ? m.mod[0][y] : m.mod[y][0];
            for (int x = 1; x < n; ++x) {
                bool c = dir ? m.mod[x][y] : m.mod[y][x];
                if (c == prev) { if (++run == 5) score += 3; else if (run > 5) ++score; }
                else { run = 1; prev = c; }
            }
        }
    // Regla 2: bloques 2x2.
    for (int y = 0; y < n - 1; ++y)
        for (int x = 0; x < n - 1; ++x) {
            bool c = m.mod[y][x];
            if (c == m.mod[y][x+1] && c == m.mod[y+1][x] && c == m.mod[y+1][x+1]) score += 3;
        }
    // Regla 3: patrón buscador-como (1011101 con 4 claros) en filas y columnas.
    auto check = [&](int x, int y, int dx, int dy) {
        static const bool pat[11] = {1,0,1,1,1,0,1,0,0,0,0};
        for (int k = 0; k < 11; ++k) {
            int xx = x + dx*k, yy = y + dy*k;
            if (xx < 0 || yy < 0 || xx >= n || yy >= n) return false;
            if (m.mod[yy][xx] != pat[k]) return false;
        }
        return true;
    };
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            if (check(x, y, 1, 0)) score += 40;
            if (check(x, y, 0, 1)) score += 40;
        }
    // Regla 4: proporción de oscuros.
    int dark = 0;
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) if (m.mod[y][x]) ++dark;
    int pct = dark * 100 / (n * n);
    score += std::abs(pct - 50) / 5 * 10;
    return score;
}

} // namespace

std::vector<std::vector<bool>> encode(const std::string &text) {
    // 1) Elegir versión mínima que quepa (modo byte, ECC M).
    const int dataLen = int(text.size());
    int version = 0, capacity = 0;
    for (int v = 1; v <= kMaxVer; ++v) {
        int cciBits = (v <= 9) ? 8 : 16;   // bits del contador de longitud (byte)
        int needBits = 4 + cciBits + dataLen * 8;
        int cap = totalDataCodewords(v) * 8;
        if (needBits <= cap) { version = v; capacity = cap; break; }
    }
    if (version == 0) return {};   // no cabe

    // 2) Bitstream: modo(0100) + contador + datos + terminador + relleno.
    std::vector<bool> bs;
    auto push = [&](uint32_t val, int bits) {
        for (int i = bits - 1; i >= 0; --i) bs.push_back((val >> i) & 1);
    };
    int cciBits = (version <= 9) ? 8 : 16;
    push(0b0100, 4);
    push(uint32_t(dataLen), cciBits);
    for (unsigned char c : text) push(c, 8);
    int termi = std::min(4, capacity - int(bs.size()));
    push(0, termi);
    while (bs.size() % 8 != 0) bs.push_back(false);
    // Bytes de relleno 0xEC / 0x11 alternos.
    bool pad = true;
    while (int(bs.size()) < capacity) { push(pad ? 0xEC : 0x11, 8); pad = !pad; }

    // 3) Bytes de datos → bloques → ECC.
    std::vector<uint8_t> dataBytes;
    for (size_t i = 0; i < bs.size(); i += 8) {
        uint8_t b = 0;
        for (int j = 0; j < 8; ++j) b = (b << 1) | (bs[i + j] ? 1 : 0);
        dataBytes.push_back(b);
    }
    const VerInfo &vi = kVer[version];
    int nBlocks = vi.g1n + vi.g2n;
    std::vector<std::vector<uint8_t>> dataBlocks, eccBlocks;
    int idx = 0;
    for (int b = 0; b < nBlocks; ++b) {
        int len = (b < vi.g1n) ? vi.g1d : vi.g2d;
        std::vector<uint8_t> blk(dataBytes.begin() + idx, dataBytes.begin() + idx + len);
        idx += len;
        dataBlocks.push_back(blk);
        eccBlocks.push_back(rsEcc(blk, vi.ecc));
    }
    // 4) Intercalar datos y ECC.
    std::vector<uint8_t> finalBytes;
    int maxData = std::max(vi.g1d, vi.g2d);
    for (int i = 0; i < maxData; ++i)
        for (auto &blk : dataBlocks)
            if (i < int(blk.size())) finalBytes.push_back(blk[i]);
    for (int i = 0; i < vi.ecc; ++i)
        for (auto &blk : eccBlocks) finalBytes.push_back(blk[i]);

    std::vector<uint8_t> finalBits;
    for (uint8_t b : finalBytes)
        for (int i = 7; i >= 0; --i) finalBits.push_back((b >> i) & 1);

    // 5) Matriz + máscara óptima.
    int size = 17 + 4 * version;
    Matrix best(size);
    int bestScore = -1;
    for (int mask = 0; mask < 8; ++mask) {
        Matrix m(size);
        drawFunction(m, version);
        placeData(m, finalBits);
        applyMask(m, mask);
        drawFormat(m, mask);
        drawVersion(m, version);
        int sc = penalty(m);
        if (bestScore < 0 || sc < bestScore) { bestScore = sc; best = m; }
    }
    return best.mod;
}

std::string toSvg(const std::string &text, int quiet) {
    auto m = encode(text);
    if (m.empty()) return {};
    int n = int(m.size());
    int dim = n + 2 * quiet;
    std::string s = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 " +
        std::to_string(dim) + " " + std::to_string(dim) + "' shape-rendering='crispEdges'>";
    s += "<rect width='" + std::to_string(dim) + "' height='" + std::to_string(dim) + "' fill='#fff'/>";
    s += "<path fill='#000' d='";
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (m[y][x])
                s += "M" + std::to_string(x + quiet) + " " + std::to_string(y + quiet) + "h1v1h-1z";
    s += "'/></svg>";
    return s;
}

} // namespace qr
