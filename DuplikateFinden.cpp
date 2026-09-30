// DuplikateFinden - sucht auf allen Festplatten (auch externen) nach doppelten Dateien.
//
//  - Doppelt heisst: exakt gleicher Inhalt (gleiche Groesse + gleiche Pruefsumme),
//    egal wie die Dateien heissen oder wo sie liegen.
//  - Es wird NICHTS geloescht oder veraendert, nur ein Bericht geschrieben.
//  - Vorgehen: erst nach Groesse gruppieren, dann die ersten 64 KB vergleichen,
//    erst dann die ganze Datei lesen. So muessen nur echte Kandidaten gelesen werden.
//  - Uebersprungen werden Windows-, Programm- und AppData-Ordner, der Papierkorb,
//    Junctions/Verknuepfungen und Cloud-Platzhalter (OneDrive "nur online").
//
// Aufruf:
//   DuplikateFinden.exe [--min GROESSE] [--alles] [--netz] [PFAD ...]
//   Ohne PFAD werden alle Festplatten, USB-Platten und Speicherkarten durchsucht.
//
// Bauen (C++17):
//   MSVC : cl /std:c++17 /EHsc /O2 /utf-8 DuplikateFinden.cpp
//   MinGW: g++ -std=c++17 -O2 -municode -static DuplikateFinden.cpp -o DuplikateFinden.exe

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <cwctype>
#  ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#    define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#  endif
#  ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#    define FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000
#  endif
#endif

namespace fs = std::filesystem;

namespace {

struct Datei {
    fs::path pfad;
    std::uintmax_t groesse = 0;
    fs::file_time_type zeit;
};

struct Einstellungen {
    std::uintmax_t minGroesse = 1024 * 1024;   // 1 MB
    bool alles = false;
    bool netz = false;
};

struct Zaehler {
    std::uint64_t dateien = 0;
    std::uint64_t fehler = 0;
};

std::string U8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string Klein(const fs::path& name) {
    std::string s = U8(name);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string GroesseText(std::uintmax_t b) {
    const char* einheit[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(b);
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    std::ostringstream os;
    os << std::fixed << std::setprecision(i == 0 ? 0 : 1) << v << ' ' << einheit[i];
    return os.str();
}

std::string Zeitstempel(const char* format) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, format);
    return os.str();
}

std::string DateiZeit(fs::file_time_type ft) {
    using namespace std::chrono;
    auto sys = time_point_cast<system_clock::duration>(ft - fs::file_time_type::clock::now() + system_clock::now());
    std::time_t t = system_clock::to_time_t(sys);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%d.%m.%Y %H:%M");
    return os.str();
}

// Ueberall ueberspringen.
bool ImmerAuslassen(const std::string& n) {
    return n == "$recycle.bin" || n == "system volume information" || n == "$windows.~bt" ||
           n == "$windows.~ws" || n == "windowsapps" || n == "$sysreset";
}

// Ueberspringen, ausser mit --alles.
bool NormalAuslassen(const std::string& n, bool aufLaufwerksebene) {
    if (n == "appdata" || n == "node_modules" || n == ".git") return true;
    if (!aufLaufwerksebene) return false;
    return n == "windows" || n == "program files" || n == "program files (x86)" ||
           n == "programdata" || n == "recovery" || n == "perflogs";
}

bool IstVerknuepfung(const fs::directory_entry& e) {
    std::error_code ec;
    if (e.is_symlink(ec)) return true;
#ifdef _WIN32
    DWORD attr = GetFileAttributesW(e.path().c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
#endif
    return false;
}

// OneDrive & Co.: Dateien, die erst beim Lesen heruntergeladen wuerden.
bool IstNurOnline(const fs::path& p) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesW(p.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    return (attr & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS |
                    FILE_ATTRIBUTE_RECALL_ON_OPEN)) != 0;
#else
    (void)p;
    return false;
#endif
}

void Durchsuchen(const fs::path& ordner, bool laufwerksebene, const Einstellungen& e,
                 std::vector<Datei>& dateien, Zaehler& z) {
    std::error_code ec;
    fs::directory_iterator it(ordner, fs::directory_options::skip_permission_denied, ec);
    if (ec) { ++z.fehler; return; }
    for (auto i = fs::begin(it); i != fs::end(it); i.increment(ec)) {
        if (ec) { ++z.fehler; break; }
        const fs::directory_entry& d = *i;
        if (IstVerknuepfung(d)) continue;
        const std::string name = Klein(d.path().filename());
        if (d.is_directory(ec)) {
            if (ImmerAuslassen(name)) continue;
            if (!e.alles && NormalAuslassen(name, laufwerksebene)) continue;
            Durchsuchen(d.path(), false, e, dateien, z);
        } else if (d.is_regular_file(ec)) {
            if (++z.dateien % 20000 == 0)
                std::cout << "  ... " << z.dateien << " Dateien angesehen\r" << std::flush;
            if (!laufwerksebene || !e.alles) {
                if (name == "pagefile.sys" || name == "hiberfil.sys" || name == "swapfile.sys") continue;
            }
            std::uintmax_t g = d.file_size(ec);
            if (ec || g < e.minGroesse) continue;
            Datei f;
            f.pfad = d.path();
            f.groesse = g;
            f.zeit = d.last_write_time(ec);
            dateien.push_back(std::move(f));
        }
    }
}

// 128-Bit-Pruefsumme (zwei unabhaengige 64-Bit-Haelften), schnell und fuer
// Duplikatsuche mehr als ausreichend.
struct Summe {
    std::uint64_t a = 0x9E3779B97F4A7C15ULL, b = 0xC2B2AE3D27D4EB4FULL;
    bool operator<(const Summe& o) const { return a != o.a ? a < o.a : b < o.b; }
    bool operator==(const Summe& o) const { return a == o.a && b == o.b; }
};

inline std::uint64_t Rotl(std::uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }

void Einarbeiten(Summe& s, const unsigned char* p, std::size_t n) {
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        std::uint64_t w;
        std::memcpy(&w, p + i, 8);
        s.a = Rotl(s.a ^ (w * 0x87C37B91114253D5ULL), 31) * 0x4CF5AD432745937FULL;
        s.b = Rotl(s.b + (w ^ 0x52DCE729ULL), 27) * 0x9E3779B185EBCA87ULL + 0x165667B19E3779F9ULL;
    }
    for (; i < n; ++i) {
        s.a = (s.a ^ p[i]) * 0x100000001B3ULL;
        s.b = Rotl(s.b + p[i], 13) * 0xC2B2AE3D27D4EB4FULL;
    }
}

// max = 0: ganze Datei.
bool Pruefsumme(const fs::path& p, std::uintmax_t max, Summe& s) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    static std::vector<unsigned char> puffer(1 << 20);
    std::uintmax_t gelesen = 0;
    while (f) {
        std::size_t wunsch = puffer.size();
        if (max && max - gelesen < wunsch) wunsch = static_cast<std::size_t>(max - gelesen);
        if (wunsch == 0) break;
        f.read(reinterpret_cast<char*>(puffer.data()), static_cast<std::streamsize>(wunsch));
        std::size_t n = static_cast<std::size_t>(f.gcount());
        if (n == 0) break;
        Einarbeiten(s, puffer.data(), n);
        gelesen += n;
    }
    return !f.bad();
}

// Teilt eine Gruppe nach Pruefsumme auf; nur Untergruppen mit >1 Datei bleiben.
std::vector<std::vector<Datei>> NachSumme(const std::vector<Datei>& gruppe, std::uintmax_t max, Zaehler& z) {
    std::map<Summe, std::vector<Datei>> m;
    for (const auto& d : gruppe) {
        Summe s;
        if (!Pruefsumme(d.pfad, max, s)) { ++z.fehler; continue; }
        m[s].push_back(d);
    }
    std::vector<std::vector<Datei>> raus;
    for (auto& kv : m)
        if (kv.second.size() > 1) raus.push_back(std::move(kv.second));
    return raus;
}

std::vector<fs::path> AlleLaufwerke(bool netz) {
    std::vector<fs::path> raus;
#ifdef _WIN32
    DWORD maske = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(maske & (1u << i))) continue;
        wchar_t wurzel[] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', 0};
        UINT typ = GetDriveTypeW(wurzel);
        if (typ == DRIVE_FIXED || typ == DRIVE_REMOVABLE || (netz && typ == DRIVE_REMOTE)) {
            std::error_code ec;
            if (fs::is_directory(wurzel, ec)) raus.emplace_back(wurzel);   // leere Kartenleser auslassen
        }
    }
#else
    (void)netz;
    raus.emplace_back("/");
#endif
    return raus;
}

fs::path ProgrammOrdner() {
#ifdef _WIN32
    wchar_t puffer[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, puffer, MAX_PATH * 4);
    if (n > 0) return fs::path(std::wstring(puffer, n)).parent_path();
#endif
    std::error_code ec;
    return fs::current_path(ec);
}

bool GroesseLesen(const std::string& s, std::uintmax_t& out) {
    std::string t = s;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    std::uintmax_t faktor = 1;
    auto endet = [&](const char* suf) {
        std::size_t l = std::strlen(suf);
        if (t.size() > l && t.compare(t.size() - l, l, suf) == 0) { t.resize(t.size() - l); return true; }
        return false;
    };
    if (endet("GB") || endet("G")) faktor = 1024ULL * 1024 * 1024;
    else if (endet("MB") || endet("M")) faktor = 1024ULL * 1024;
    else if (endet("KB") || endet("K")) faktor = 1024ULL;
    else if (endet("B")) faktor = 1;
    try {
        std::size_t pos = 0;
        double v = std::stod(t, &pos);
        if (pos != t.size() || v < 0) return false;
        out = static_cast<std::uintmax_t>(v * static_cast<double>(faktor));
        return true;
    } catch (...) {
        return false;
    }
}

void Hilfe() {
    std::cout <<
        "DuplikateFinden - sucht doppelte Dateien (gleicher Inhalt) auf allen Laufwerken.\n"
        "Es wird nichts geloescht, nur ein Bericht erstellt.\n\n"
        "  --min GROESSE  nur Dateien ab dieser Groesse (Standard 1MB, z. B. 100KB, 10MB, 0)\n"
        "  --alles        auch Windows-, Programm- und AppData-Ordner durchsuchen\n"
        "  --netz         auch Netzlaufwerke durchsuchen\n"
        "  PFAD ...       nur diese Ordner/Laufwerke durchsuchen (z. B. Y:\\ E:\\Fotos)\n";
}

int Programm(const std::vector<fs::path>& args) {
    Einstellungen e;
    std::vector<fs::path> wurzeln;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string a = U8(args[i]);
        if (a == "--alles") e.alles = true;
        else if (a == "--netz") e.netz = true;
        else if (a == "--min" && i + 1 < args.size()) {
            if (!GroesseLesen(U8(args[++i]), e.minGroesse)) {
                std::cout << "Ungueltige Groesse: " << U8(args[i]) << "\n";
                return 2;
            }
        } else if (a == "--hilfe" || a == "-h" || a == "/?") { Hilfe(); return 0; }
        else if (!a.empty() && a[0] == '-') { std::cout << "Unbekannte Option: " << a << "\n\n"; Hilfe(); return 2; }
        else wurzeln.push_back(args[i]);
    }
    if (e.minGroesse == 0) e.minGroesse = 1;   // leere Dateien sind nie interessant
    if (wurzeln.empty()) wurzeln = AlleLaufwerke(e.netz);

    std::cout << "Durchsuche (Dateien ab " << GroesseText(e.minGroesse) << "):\n";
    for (const auto& w : wurzeln) std::cout << "  " << U8(w) << "\n";
    std::cout << "\n";

    const auto start = std::chrono::steady_clock::now();
    Zaehler z;
    std::vector<Datei> dateien;
    for (const auto& w : wurzeln) {
        std::error_code ec;
        if (!fs::is_directory(w, ec)) { std::cout << "  nicht gefunden: " << U8(w) << "\n"; continue; }
        std::cout << "Lese " << U8(w) << " ...                    \n";
        fs::path wurzel = w;
        bool laufwerk = wurzel.has_root_path() && wurzel.relative_path().empty();
        Durchsuchen(wurzel, laufwerk, e, dateien, z);
    }
    std::cout << "\n" << z.dateien << " Dateien angesehen, " << dateien.size()
              << " gross genug fuer den Vergleich.\n";

    // Doppelte Pfade entfernen (falls jemand z. B. "Y:\" und "Y:\Fotos" angibt).
    std::sort(dateien.begin(), dateien.end(), [](const Datei& x, const Datei& y) { return x.pfad < y.pfad; });
    dateien.erase(std::unique(dateien.begin(), dateien.end(),
                              [](const Datei& x, const Datei& y) { return x.pfad == y.pfad; }),
                  dateien.end());

    // 1. Nach Groesse gruppieren.
    std::unordered_map<std::uintmax_t, std::vector<Datei>> nachGroesse;
    for (auto& d : dateien) nachGroesse[d.groesse].push_back(std::move(d));
    dateien.clear();
    dateien.shrink_to_fit();

    std::vector<std::vector<Datei>> kandidaten;
    std::size_t kandDateien = 0;
    for (auto& kv : nachGroesse) {
        if (kv.second.size() < 2) continue;
        std::vector<Datei> g;
        for (auto& d : kv.second)
            if (!IstNurOnline(d.pfad)) g.push_back(std::move(d));
        if (g.size() > 1) { kandDateien += g.size(); kandidaten.push_back(std::move(g)); }
    }
    nachGroesse.clear();
    std::cout << kandDateien << " Dateien haben eine gleich grosse Partnerdatei - vergleiche Inhalt ...\n";

    // 2. Erste 64 KB vergleichen, 3. ganze Datei vergleichen.
    std::vector<std::vector<Datei>> duplikate;
    std::size_t erledigt = 0;
    for (const auto& g : kandidaten) {
        for (auto& teil : NachSumme(g, 64 * 1024, z)) {
            if (teil.front().groesse <= 64 * 1024) {
                duplikate.push_back(std::move(teil));
            } else {
                for (auto& voll : NachSumme(teil, 0, z)) duplikate.push_back(std::move(voll));
            }
        }
        erledigt += g.size();
        std::cout << "  " << erledigt << " / " << kandDateien << "\r" << std::flush;
    }
    std::cout << "\n\n";

    // Groesste Platzverschwendung zuerst.
    std::sort(duplikate.begin(), duplikate.end(), [](const auto& x, const auto& y) {
        return x.front().groesse * (x.size() - 1) > y.front().groesse * (y.size() - 1);
    });
    for (auto& g : duplikate)
        std::sort(g.begin(), g.end(), [](const Datei& x, const Datei& y) { return x.zeit > y.zeit; });

    std::uintmax_t verschwendet = 0;
    std::size_t doppelteDateien = 0;
    for (const auto& g : duplikate) {
        verschwendet += g.front().groesse * (g.size() - 1);
        doppelteDateien += g.size() - 1;
    }

    // Berichte schreiben.
    fs::path ordner = ProgrammOrdner() / "Protokolle";
    std::error_code ec;
    fs::create_directories(ordner, ec);
    const std::string zeit = Zeitstempel("%Y-%m-%d_%H-%M-%S");
    fs::path txtPfad = ordner / ("Duplikate_" + zeit + ".txt");
    fs::path csvPfad = ordner / ("Duplikate_" + zeit + ".csv");

    std::ofstream txt(txtPfad, std::ios::binary);
    std::ofstream csv(csvPfad, std::ios::binary);
    txt << "\xEF\xBB\xBF";
    csv << "\xEF\xBB\xBF" << "Gruppe;Groesse (Bytes);Geaendert;Neueste;Pfad\r\n";
    txt << "Duplikate - " << Zeitstempel("%d.%m.%Y %H:%M") << "\r\n";
    txt << "Gruppen: " << duplikate.size() << ", ueberzaehlige Kopien: " << doppelteDateien
        << ", verschwendeter Platz: " << GroesseText(verschwendet) << "\r\n";
    txt << "In jeder Gruppe steht die neueste Datei oben (mit * markiert).\r\n\r\n";

    int nr = 0;
    for (const auto& g : duplikate) {
        ++nr;
        txt << "Gruppe " << nr << ": " << g.size() << " x " << GroesseText(g.front().groesse)
            << "  (" << GroesseText(g.front().groesse * (g.size() - 1)) << " doppelt)\r\n";
        bool erste = true;
        for (const auto& d : g) {
            txt << (erste ? "  * " : "    ") << DateiZeit(d.zeit) << "  " << U8(d.pfad) << "\r\n";
            std::string p = U8(d.pfad);
            std::string pq;
            for (char c : p) { if (c == '"') pq += '"'; pq += c; }
            csv << nr << ';' << d.groesse << ';' << DateiZeit(d.zeit) << ';' << (erste ? "ja" : "")
                << ";\"" << pq << "\"\r\n";
            erste = false;
        }
        txt << "\r\n";
    }

    // Kurzfassung auf dem Bildschirm.
    std::cout << "Gefunden: " << duplikate.size() << " Gruppen mit doppeltem Inhalt, "
              << doppelteDateien << " ueberzaehlige Kopien, zusammen " << GroesseText(verschwendet) << ".\n";
    if (!duplikate.empty()) {
        std::cout << "\nDie 10 groessten:\n";
        for (std::size_t i = 0; i < duplikate.size() && i < 10; ++i) {
            const auto& g = duplikate[i];
            std::cout << "  " << g.size() << " x " << GroesseText(g.front().groesse) << ":\n";
            for (std::size_t j = 0; j < g.size() && j < 4; ++j) std::cout << "      " << U8(g[j].pfad) << "\n";
            if (g.size() > 4) std::cout << "      ... und " << g.size() - 4 << " weitere\n";
        }
    }
    const auto sek = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << "\nNicht lesbar (uebersprungen): " << z.fehler << "\n";
    std::cout << "Dauer: " << sek / 60 << " min " << sek % 60 << " s\n";
    std::cout << "Bericht:      " << U8(txtPfad) << "\n";
    std::cout << "Fuer Excel:   " << U8(csvPfad) << "\n";
    std::cout << "\nEs wurde nichts geloescht.\n";
    return 0;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<fs::path> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return Programm(args);
}
#else
int main(int argc, char** argv) {
    std::vector<fs::path> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return Programm(args);
}
#endif
